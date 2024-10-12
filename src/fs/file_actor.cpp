// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "file_actor.h"
#include "net/names.h"
#include "model/folder_info.h"
#include "model/file_info.h"
#include "model/diff/modify/append_block.h"
#include "model/diff/modify/clone_block.h"
#include "model/diff/modify/clone_file.h"
#include "model/diff/modify/finish_file.h"
#include "model/diff/modify/finish_file_ack.h"
#include "utils.h"
#include <fstream>

using namespace syncspirit::fs;

file_actor_t::write_ack_t::write_ack_t(const model::diff::modify::block_transaction_t &txn_) noexcept
    : txn{txn_}, success{false} {}

auto file_actor_t::write_ack_t::operator()(outcome::result<void> result) noexcept -> outcome::result<void> {
    success = (bool)result;
    return result;
}

file_actor_t::write_ack_t::~write_ack_t() {
    if (!success) {
        ++txn.errors;
    }
}

file_actor_t::file_actor_t(config_t &cfg)
    : r::actor_base_t{cfg}, cluster{cfg.cluster}, rw_cache(cfg.mru_size), ro_cache(cfg.mru_size) {}

void file_actor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) {
        p.set_identity(net::names::fs_actor, false);
        log = utils::get_logger(identity);
    });
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
        p.register_name(net::names::fs_actor, address);
        p.discover_name(net::names::coordinator, coordinator, true).link(false).callback([&](auto phase, auto &ee) {
            if (!ee && phase == r::plugin::registry_plugin_t::phase_t::linking) {
                auto p = get_plugin(r::plugin::starter_plugin_t::class_identity);
                auto plugin = static_cast<r::plugin::starter_plugin_t *>(p);
                plugin->subscribe_actor(&file_actor_t::on_model_update, coordinator);
            }
        });
    });
    plugin.with_casted<r::plugin::starter_plugin_t>(
        [&](auto &p) { p.subscribe_actor(&file_actor_t::on_block_request); });
}

void file_actor_t::on_start() noexcept {
    LOG_TRACE(log, "on_start");
    r::actor_base_t::on_start();
}

void file_actor_t::shutdown_start() noexcept {
    LOG_TRACE(log, "shutdown_start");
    r::actor_base_t::shutdown_start();
    rw_cache.clear();
}

void file_actor_t::on_model_update(model::message::model_update_t &message) noexcept {
    LOG_TRACE(log, "on_model_update");
    auto &diff = *message.payload.diff;
    auto r = diff.visit(*this, nullptr);
    if (!r) {
        auto ee = make_error(r.assume_error());
        do_shutdown(ee);
    }
}

void file_actor_t::on_block_request(message::block_request_t &message) noexcept {
    LOG_TRACE(log, "on_block_request");
    auto &p = message.payload;
    auto &dest = p.reply_to;
    auto &req_ptr = message.payload.remote_request;
    auto &req = *req_ptr;
    auto folder = cluster->get_folders().by_id(req.folder());
    auto folder_info = folder->get_folder_infos().by_device(*cluster->get_device());
    auto file_info = folder_info->get_file_infos().by_name(req.name());
    auto &path = file_info->get_path();
    auto file_opt = open_file_ro(path, true);
    auto ec = sys::error_code{};
    auto data = std::string{};
    if (!file_opt) {
        ec = file_opt.assume_error();
        LOG_ERROR(log, "error opening file {}: {}", path.string(), ec.message());
    } else {
        auto &file = file_opt.assume_value();
        auto block_opt = file->read(req.offset(), req.size());
        if (!block_opt) {
            ec = block_opt.assume_error();
            LOG_WARN(log, "error requesting block; offset = {}, size = {} :: {} ", req.offset(), req.size(),
                     ec.message());
        } else {
            data = std::move(block_opt.assume_value());
        }
    }
    send<payload::block_response_t>(dest, std::move(req_ptr), ec, std::move(data));
}

auto file_actor_t::reflect(model::file_info_ptr_t &file_ptr) noexcept -> outcome::result<void> {
    auto &file = *file_ptr;
    auto &path = file.get_path();
    sys::error_code ec;

    if (file.is_deleted()) {
        if (bfs::exists(path, ec)) {
            LOG_DEBUG(log, "removing {}", path.string());
            auto ok = bfs::remove_all(path, ec);
            if (!ok) {
                LOG_ERROR(log, "error removing {} : {}", path.string(), ec.message());
                return ec;
            }
        } else {
            LOG_TRACE(log, "{} already abscent, noop", path.string());
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
            LOG_TRACE(log, "touching file {} ({} bytes)", path.string(), sz);
            auto file_opt = open_file_rw(path, &file);
            if (!file_opt) {
                auto &err = file_opt.assume_error();
                LOG_ERROR(log, "cannot open file: {}: {}", path.string(), err.message());
                return err;
            }
        } else {
            LOG_TRACE(log, "touching empty file {}", path.string());
            std::ofstream out;
            out.exceptions(out.failbit | out.badbit);
            try {
                out.open(path.string());
            } catch (const std::ios_base::failure &e) {
                LOG_ERROR(log, "error creating {} : {}", path.string(), e.code().message());
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
        LOG_DEBUG(log, "creating directory {}", path.string());
        bfs::create_directory(path, ec);
        if (ec) {
            return ec;
        }
    } else if (file.is_link()) {
        auto target = bfs::path(file.get_link_target());
        LOG_DEBUG(log, "creating symlink {} -> {}", path.string(), target.string());

        bool attempt_create =
            !bfs::exists(path, ec) || !bfs::is_symlink(path, ec) || (bfs::read_symlink(path, ec) != target);
        if (attempt_create) {
            bfs::create_symlink(target, path, ec);
            if (ec) {
                LOG_WARN(log, "error symlinking {} -> {} : {}", path.string(), target.string(), ec.message());
                return ec;
            }
        } else {
            LOG_TRACE(log, "no need to create symlink {} -> {}", path.string(), target.string());
        }
    }

    return outcome::success();
}

auto file_actor_t::operator()(const model::diff::modify::clone_file_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    auto folder = cluster->get_folders().by_id(diff.folder_id);
    auto file_info = folder->get_folder_infos().by_device_id(diff.peer_id);
    auto file = file_info->get_file_infos().by_name(diff.proto_file.name());
    auto r = reflect(file);
    return r ? diff.visit_next(*this, custom) : r;
}

auto file_actor_t::operator()(const model::diff::modify::finish_file_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    auto folder = cluster->get_folders().by_id(diff.folder_id);
    auto file_info = folder->get_folder_infos().by_device(*cluster->get_device());
    auto file = file_info->get_file_infos().by_name(diff.file_name);
    assert(file->get_source()->is_locally_available());

    auto path = file->get_path().string();
    auto backend = rw_cache.get(path);
    if (!backend) {
        LOG_DEBUG(log, "attempt to flush non-opened file {}, re-open it as temporal", path);
        auto path_tmp = make_temporal(file->get_path());
        auto result = open_file_rw(path_tmp, file);
        if (!result) {
            auto &ec = result.assume_error();
            LOG_ERROR(log, "cannot open file: {}: {}", path_tmp.string(), ec.message());
            return ec;
        }
        backend = std::move(result.assume_value());
    }

    rw_cache.remove(backend);
    auto ok = backend->close(true);
    if (!ok) {
        auto &ec = ok.assume_error();
        LOG_ERROR(log, "cannot close file: {}: {}", path, ec.message());
        return ec;
    }

    LOG_INFO(log, "file {} ({} bytes) is now locally available", path, file->get_size());

    auto ack = model::diff::cluster_diff_ptr_t{};
    ack = new model::diff::modify::finish_file_ack_t(*file);
    send<model::payload::model_update_t>(coordinator, std::move(ack), this);
    return diff.visit_next(*this, custom);
}

auto file_actor_t::operator()(const model::diff::modify::append_block_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    auto ack = write_ack_t(diff);
    auto folder = cluster->get_folders().by_id(diff.folder_id);
    auto file_info = folder->get_folder_infos().by_device_id(diff.device_id);
    auto file = file_info->get_file_infos().by_name(diff.file_name);
    auto &path = file->get_path();
    auto path_str = path.string();
    auto file_opt = open_file_rw(path, file);
    if (!file_opt) {
        auto &err = file_opt.assume_error();
        LOG_ERROR(log, "cannot open file: {}: {}", path_str, err.message());
        return err;
    }

    auto block_index = diff.block_index;
    auto offset = file->get_block_offset(block_index);
    auto &backend = file_opt.value();
    auto r = ack(backend->write(offset, diff.data));
    return r ? diff.visit_next(*this, custom) : r;
}

auto file_actor_t::get_source_for_cloning(model::file_info_ptr_t &source, const file_ptr_t &target_backend) noexcept
    -> outcome::result<file_ptr_t> {
    auto source_path = source->get_path();
    if (source_path == target_backend->get_path()) {
        return target_backend;
    }

    auto source_tmp = make_temporal(source_path);

    if (auto cached = rw_cache.get(source_path.string()); cached) {
        return cached;
    } else if (auto cached = rw_cache.get(source_tmp.string()); cached) {
        return cached;
    } else if (auto cached = ro_cache.get(source_tmp.string()); cached) {
        return cached;
    } else if (auto cached = ro_cache.get(source_tmp.string()); cached) {
        return cached;
    } else if (auto opt = open_file_ro(source_tmp, false)) {
        return opt.assume_value();
    }

    return open_file_ro(source_path, false);
}

auto file_actor_t::operator()(const model::diff::modify::clone_block_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    auto ack = write_ack_t(diff);
    auto folder = cluster->get_folders().by_id(diff.folder_id);
    auto target_folder_info = folder->get_folder_infos().by_device_id(diff.device_id);
    auto target = target_folder_info->get_file_infos().by_name(diff.file_name);

    auto source_folder_info = folder->get_folder_infos().by_device_id(diff.source_device_id);
    auto source = source_folder_info->get_file_infos().by_name(diff.source_file_name);

    auto &target_path = target->get_path();
    auto file_opt = open_file_rw(target_path, target);
    if (!file_opt) {
        auto &err = file_opt.assume_error();
        LOG_ERROR(log, "cannot open file: {}: {}", target_path.string(), err.message());
        return err;
    }
    auto target_backend = std::move(file_opt.assume_value());
    auto source_backend_opt = get_source_for_cloning(source, target_backend);
    if (!source_backend_opt) {
        auto ec = source_backend_opt.assume_error();
        LOG_ERROR(log, "cannot open file for cloning: {}: {}", target_path.string(), ec.message());
    }

    auto &source_backend = source_backend_opt.assume_value();
    auto &block = source->get_blocks().at(diff.source_block_index);
    auto target_offset = target->get_block_offset(diff.block_index);
    auto source_offset = source->get_block_offset(diff.source_block_index);
    auto r = ack(target_backend->copy(target_offset, *source_backend, source_offset, block->get_size()));
    return r ? diff.visit_next(*this, custom) : r;
}

auto file_actor_t::open_file_rw(const boost::filesystem::path &path, model::file_info_ptr_t info) noexcept
    -> outcome::result<file_ptr_t> {
    auto item = rw_cache.get(path.string());
    if (item) {
        return item;
    }

    auto size = info->get_size();
    LOG_TRACE(log, "open_file (model), path = {} ({} bytes)", path.string(), size);
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
    rw_cache.put(ptr);
    return ptr;
}

auto file_actor_t::open_file_ro(const bfs::path &path, bool use_cache) noexcept -> outcome::result<file_ptr_t> {
    LOG_TRACE(log, "open_file (by path), path = {}", path.string());
    if (use_cache) {
        auto file = rw_cache.get(path.string());
        if (file) {
            return file;
        }
    }

    auto opt = file_t::open_read(path);
    if (!opt) {
        return opt.assume_error();
    }
    return file_ptr_t(new file_t(std::move(opt.assume_value())));
}
