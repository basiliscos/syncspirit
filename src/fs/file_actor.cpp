// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#include "file_actor.h"
#include "net/names.h"
#include "model/diff/modify/append_block.h"
#include "model/diff/modify/clone_block.h"
#include "model/diff/modify/clone_file.h"
#include "model/diff/modify/flush_file.h"
#include "utils.h"
#include <fstream>

using namespace syncspirit::fs;

file_actor_t::file_actor_t(config_t &cfg) : r::actor_base_t{cfg}, cluster{cfg.cluster}, files_cache(cfg.mru_size) {
    log = utils::get_logger("fs.file_actor");
}

void file_actor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) { p.set_identity("fs::file_actor", false); });
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
        p.discover_name(net::names::coordinator, coordinator, true).link(false).callback([&](auto phase, auto &ee) {
            if (!ee && phase == r::plugin::registry_plugin_t::phase_t::linking) {
                auto p = get_plugin(r::plugin::starter_plugin_t::class_identity);
                auto plugin = static_cast<r::plugin::starter_plugin_t *>(p);
                plugin->subscribe_actor(&file_actor_t::on_model_update, coordinator);
                plugin->subscribe_actor(&file_actor_t::on_block_update, coordinator);
            }
        });
    });
}

void file_actor_t::on_start() noexcept {
    LOG_TRACE(log, "{}, on_start", identity);
    r::actor_base_t::on_start();
}

void file_actor_t::shutdown_start() noexcept {
    LOG_TRACE(log, "{}, shutdown_start", identity);
    r::actor_base_t::shutdown_start();
    files_cache.clear();
}

void file_actor_t::on_model_update(model::message::model_update_t &message) noexcept {
    LOG_TRACE(log, "{}, on_model_update", identity);
    auto &diff = *message.payload.diff;
    auto r = diff.visit(*this);
    if (!r) {
        auto ee = make_error(r.assume_error());
        do_shutdown(ee);
    }
}

void file_actor_t::on_block_update(model::message::block_update_t &message) noexcept {
    LOG_TRACE(log, "{}, on_block_update", identity);
    auto &diff = *message.payload.diff;
    auto r = diff.visit(*this);
    if (!r) {
        auto ee = make_error(r.assume_error());
        do_shutdown(ee);
    }
}

auto file_actor_t::reflect(model::file_info_ptr_t &file_ptr) noexcept -> outcome::result<void> {
    auto &file = *file_ptr;
    auto &path = file.get_path();
    sys::error_code ec;

    if (file.is_deleted()) {
        if (bfs::exists(path, ec)) {
            LOG_DEBUG(log, "{} removing {}", identity, path.string());
            auto ok = bfs::remove_all(path, ec);
            if (!ok) {
                LOG_ERROR(log, "{},  error removing {} : {}", identity, path.string(), ec.message());
                return ec;
            }
        } else {
            LOG_TRACE(log, "{}, {} already abscent, noop", identity, path.string());
        }
        return outcome::success();
    }

    auto parent = path.parent_path();

    bool exists = bfs::exists(parent, ec);
    if (!exists) {
        bfs::create_directories(parent, ec);
        if (ec) {
            return ec;
        }
    }

    if (file.is_file()) {
        auto sz = file.get_size();
        if (file.is_locally_available() && sz) {
            return outcome::success();
        }

        bool temporal = sz > 0;
        if (temporal) {
            LOG_TRACE(log, "{}, touching file {} ({} bytes)", identity, path.string(), sz);
            auto file_opt = open_file_rw(path, &file);
            if (!file_opt) {
                auto &err = file_opt.assume_error();
                LOG_ERROR(log, "{}, cannot open file: {}: {}", identity, path.string(), err.message());
                return err;
            }
        } else {
            LOG_TRACE(log, "{}, touching empty file {}", identity, path.string());
            std::ofstream out;
            out.exceptions(out.failbit | out.badbit);
            try {
                out.open(path.string());
            } catch (const std::ios_base::failure &e) {
                LOG_ERROR(log, "{}, error creating {} : {}", identity, path.string(), e.code().message());
                return sys::errc::make_error_code(sys::errc::io_error);
            }
            out.close();

            std::time_t modified = file.get_modified_s();
            bfs::last_write_time(path, modified, ec);
            if (ec) {
                return ec;
            }
        }
    } else if (file.is_dir()) {
        LOG_DEBUG(log, "{}, creating directory {}", identity, path.string());
        bfs::create_directory(path, ec);
        if (ec) {
            return ec;
        }
    } else if (file.is_link()) {
        auto target = bfs::path(file.get_link_target());
        LOG_DEBUG(log, "{}, creating symlink {} -> {}", identity, path.string(), target.string());

        bool attempt_create =
            !bfs::exists(path, ec) || !bfs::is_symlink(path, ec) || (bfs::read_symlink(path, ec) != target);
        if (attempt_create) {
            bfs::create_symlink(target, path, ec);
            if (ec) {
                LOG_WARN(log, "{}, error symlinking {} -> {} : {}", identity, path.string(), target.string(),
                         ec.message());
                return ec;
            }
        } else {
            LOG_TRACE(log, "{}, no need to create symlink {} -> {}", identity, path.string(), target.string());
        }
    }

    return outcome::success();
}

auto file_actor_t::operator()(const model::diff::modify::clone_file_t &diff) noexcept -> outcome::result<void> {
    auto folder = cluster->get_folders().by_id(diff.folder_id);
    auto file_info = folder->get_folder_infos().by_device_id(diff.device_id);
    auto file = file_info->get_file_infos().by_name(diff.file.name());
    return reflect(file);
}

auto file_actor_t::operator()(const model::diff::modify::flush_file_t &diff) noexcept -> outcome::result<void> {
    auto folder = cluster->get_folders().by_id(diff.folder_id);
    auto file_info = folder->get_folder_infos().by_device_id(diff.device_id);
    auto file = file_info->get_file_infos().by_name(diff.file_name);
    assert(file->is_locally_available());

    auto path = file->get_path().string();
    auto backend = files_cache.get(path);
    if (!backend) {
        LOG_ERROR(log, "{}, attempt to flush non-opend file {}", identity, path);
        auto ec = sys::errc::make_error_code(sys::errc::io_error);
        return ec;
    }

    files_cache.remove(backend);
    auto ok = backend->close(true);
    if (!ok) {
        auto &ec = ok.assume_error();
        LOG_ERROR(log, "{}, cannot close file: {}: {}", identity, path, ec.message());
        return ec;
    }

    LOG_INFO(log, "{}, file {} ({} bytes) is now locally available", identity, path, file->get_size());
    return outcome::success();
}

auto file_actor_t::operator()(const model::diff::modify::append_block_t &diff) noexcept -> outcome::result<void> {
    auto folder = cluster->get_folders().by_id(diff.folder_id);
    auto file_info = folder->get_folder_infos().by_device_id(diff.device_id);
    auto file = file_info->get_file_infos().by_name(diff.file_name);
    auto &path = file->get_path();
    auto path_str = path.string();
    auto file_opt = open_file_rw(path, file);
    if (!file_opt) {
        auto &err = file_opt.assume_error();
        LOG_ERROR(log, "{}, cannot open file: {}: {}", identity, path_str, err.message());
        return err;
    }

    auto block_index = diff.block_index;
    auto offset = file->get_block_offset(block_index);
    auto &backend = file_opt.value();
    return backend->write(offset, diff.data);
}

auto file_actor_t::operator()(const model::diff::modify::clone_block_t &diff) noexcept -> outcome::result<void> {
    auto folder = cluster->get_folders().by_id(diff.folder_id);
    auto target_folder_info = folder->get_folder_infos().by_device_id(diff.device_id);
    auto target = target_folder_info->get_file_infos().by_name(diff.file_name);

    auto source_folder_info = folder->get_folder_infos().by_device_id(diff.source_device_id);
    auto source = source_folder_info->get_file_infos().by_name(diff.source_file_name);

    auto &target_path = target->get_path();
    auto file_opt = open_file_rw(target_path, target);
    if (!file_opt) {
        auto &err = file_opt.assume_error();
        LOG_ERROR(log, "{}, cannot open file: {}: {}", identity, target_path.string(), err.message());
        return err;
    }
    auto target_backend = std::move(file_opt.assume_value());
    auto source_backend = file_ptr_t{};

    auto source_path = source->get_path();
    if (source_path == target_path) {
        source_backend = target_backend;
    } else if (auto cached_source = files_cache.get(source_path.string()); cached_source) {
        source_backend = cached_source;
    } else {
        if (!source->is_locally_available()) {
            assert(source->is_partly_available());
            source_path = make_temporal(source_path);
        }

        auto source_opt = open_file_ro(source_path);
        if (!source_opt) {
            auto &ec = source_opt.assume_error();
            LOG_ERROR(log, "{}, cannot open file: {}: {}", identity, source_path.string(), ec.message());
            return ec;
        }
        source_backend = std::move(source_opt.assume_value());
    }

    auto &block = source->get_blocks().at(diff.source_block_index);
    auto target_offset = target->get_block_offset(diff.block_index);
    auto source_offset = source->get_block_offset(diff.source_block_index);
    return target_backend->copy(target_offset, *source_backend, source_offset, block->get_size());
}

auto file_actor_t::open_file_rw(const boost::filesystem::path &path, model::file_info_ptr_t info) noexcept
    -> outcome::result<file_ptr_t> {
    auto item = files_cache.get(path.string());
    if (item) {
        return item;
    }

    auto size = info->get_size();
    LOG_TRACE(log, "{}, open_file (model), path = {} ({} bytes)", identity, path.string(), size);
    // auto opt = file_t::open_write(path, )
    // bfs::path operational_path = temporal ? make_temporal(path) : path;

    auto parent = path.parent_path();
    sys::error_code ec;

    bool exists = bfs::exists(parent, ec);
    if (!exists) {
        bfs::create_directories(parent, ec);
        if (ec) {
            return ec;
        }
    }

    auto option = file_t::open_write(info);
    if (!option) {
        return option.assume_error();
    }
    auto ptr = file_ptr_t(new file_t(std::move(option.assume_value())));
    files_cache.put(ptr);
    return std::move(ptr);

    files_cache.put(item);
    return std::move(item);
}

auto file_actor_t::open_file_ro(const bfs::path &path) noexcept -> outcome::result<file_ptr_t> {
    LOG_TRACE(log, "{}, open_file (by path), path = {}", identity, path.string());
    auto opt = file_t::open_read(path);
    if (!opt) {
        auto &ec = opt.assume_error();
        LOG_ERROR(log, "{}, error opening file {}: {}", identity, path.string(), ec.message());
        return opt.assume_error();
    }
    return file_ptr_t(new file_t(std::move(opt.assume_value())));
}
