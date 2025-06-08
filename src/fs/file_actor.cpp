// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "file_actor.h"
#include "net/names.h"
#include "model/folder_info.h"
#include "model/file_info.h"
#include "model/diff/modify/append_block.h"
#include "model/diff/modify/clone_block.h"
#include "model/diff/advance/remote_copy.h"
#include "model/diff/advance/remote_win.h"
#include "model/diff/modify/finish_file.h"
#include "utils.h"
#include "utils/io.h"
#include "utils/format.hpp"
#include "utils/platform.h"

using namespace syncspirit::fs;
using namespace syncspirit::proto;

namespace {
namespace resource {
r::plugin::resource_id_t controller = 0;
} // namespace resource
} // namespace

file_actor_t::write_guard_t::write_guard_t(file_actor_t &actor_,
                                           const model::diff::modify::block_transaction_t &txn_) noexcept
    : actor{actor_}, txn{txn_}, success{false} {}

auto file_actor_t::write_guard_t::operator()(outcome::result<void> result) noexcept -> outcome::result<void> {
    success = (bool)result;
    if (!success) {
        actor.log->debug("I/O failure on {}: {}", txn.file_name, result.assume_error().message());
    }
    return result;
}

file_actor_t::write_guard_t::~write_guard_t() {
    auto reply = success ? txn.ack() : txn.rej();
    actor.send<model::payload::model_update_t>(actor.coordinator, std::move(reply));
}

file_actor_t::file_actor_t(config_t &cfg)
    : r::actor_base_t{cfg}, cluster{cfg.cluster}, sequencer(cfg.sequencer), rw_cache(std::move(cfg.rw_cache)),
      ro_cache(rw_cache->get_max_items()) {
    assert(sequencer);
}

void file_actor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) {
        p.set_identity(net::names::fs_actor, false);
        log = utils::get_logger(identity);
    });
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
        p.register_name(net::names::fs_actor, address);
        p.discover_name(net::names::coordinator, coordinator, false).link(false).callback([&](auto phase, auto &ee) {
            if (!ee && phase == r::plugin::registry_plugin_t::phase_t::linking) {
                auto p = get_plugin(r::plugin::starter_plugin_t::class_identity);
                auto plugin = static_cast<r::plugin::starter_plugin_t *>(p);
                plugin->subscribe_actor(&file_actor_t::on_controller_up, coordinator);
                plugin->subscribe_actor(&file_actor_t::on_controller_down, coordinator);
            }
        });
    });
    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
        p.subscribe_actor(&file_actor_t::on_block_request);
        p.subscribe_actor(&file_actor_t::on_model_update);
    });
}

void file_actor_t::on_start() noexcept {
    LOG_TRACE(log, "on_start");
    r::actor_base_t::on_start();
}

void file_actor_t::shutdown_start() noexcept {
    LOG_TRACE(log, "shutdown_start");
    r::actor_base_t::shutdown_start();
    rw_cache->clear();
}

void file_actor_t::on_model_update(model::message::model_update_t &message) noexcept {
    LOG_TRACE(log, "on_model_update");
    auto &payload = message.payload;
    auto &diff = payload.diff;
    auto r = diff->visit(*this, nullptr);
    if (!r) {
        auto ee = make_error(r.assume_error());
        return do_shutdown(ee);
    }
    send<model::payload::model_update_t>(coordinator, std::move(diff), payload.custom);
}

void file_actor_t::on_block_request(message::block_request_t &message) noexcept {
    LOG_TRACE(log, "on_block_request");
    auto &p = message.payload;
    auto &dest = p.reply_to;
    auto &req = message.payload.remote_request;
    auto folder = cluster->get_folders().by_id(get_folder(req));
    auto folder_info = folder->get_folder_infos().by_device(*cluster->get_device());
    auto file_info = folder_info->get_file_infos().by_name(get_name(req));
    auto &path = file_info->get_path();
    auto file_opt = open_file_ro(path, true);
    auto ec = sys::error_code{};
    auto data = utils::bytes_t{};
    if (!file_opt) {
        ec = file_opt.assume_error();
        LOG_ERROR(log, "error opening file {}: {}", path.string(), ec.message());
    } else {
        auto &file = file_opt.assume_value();
        auto offset = get_offset(req);
        auto size = get_size(req);
        auto block_opt = file->read(offset, size);
        if (!block_opt) {
            ec = block_opt.assume_error();
            LOG_WARN(log, "error requesting block; offset = {}, size = {} :: {} ", offset, size, ec.message());
        } else {
            data = std::move(block_opt.assume_value());
        }
    }
    send<payload::block_response_t>(dest, std::move(req), ec, std::move(data));
}

void file_actor_t::on_controller_up(net::message::controller_up_t &message) noexcept {
    LOG_DEBUG(log, "on_controller_up, {}", (const void *)message.payload.controller.get());
    resources->acquire(resource::controller);
}

void file_actor_t::on_controller_down(net::message::controller_down_t &message) noexcept {
    LOG_DEBUG(log, "on_controller_down, {}", (const void *)message.payload.controller.get());
    resources->release(resource::controller);
}

auto file_actor_t::reflect(model::file_info_ptr_t &file_ptr, const bfs::path &path) noexcept -> outcome::result<void> {
    auto &file = *file_ptr;
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
            auto out = utils::ofstream_t(path, utils::ofstream_t::trunc);
            if (!out) {
                auto ec = sys::error_code{errno, sys::system_category()};
                LOG_ERROR(log, "error creating {}: {}", path.string(), ec.message());
                return ec;
            }
            out.close();
            bfs::last_write_time(path, from_unix(file.get_modified_s()), ec);
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
        if (utils::platform_t::symlinks_supported()) {
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
        } else {
            LOG_TRACE(log, "symlinks are not supported by platform, no I/O for {}", path.string());
        }
    }

    return outcome::success();
}

auto file_actor_t::operator()(const model::diff::advance::remote_copy_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    auto folder = cluster->get_folders().by_id(diff.folder_id);
    auto file_info = folder->get_folder_infos().by_device_id(diff.peer_id);
    auto name = get_name(diff.proto_source);
    auto file = file_info->get_file_infos().by_name(name);
    auto r = reflect(file, file->get_path());
    return r ? diff.visit_next(*this, custom) : r;
}

auto file_actor_t::operator()(const model::diff::advance::remote_win_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    auto folder = cluster->get_folders().by_id(diff.folder_id);
    auto folder_info = folder->get_folder_infos();
    auto file_info = folder_info.by_device_id(diff.peer_id);
    auto source_name = get_name(diff.proto_source);
    auto local_name = get_name(diff.proto_local);
    auto file = file_info->get_file_infos().by_name(source_name);
    auto &source_path = file->get_path();
    auto target_path = folder->get_path() / local_name;
    LOG_DEBUG(log, "renaming {} -> {}", source_path, target_path);
    auto ec = sys::error_code{};
    bfs::rename(source_path, target_path);
    return !ec ? diff.visit_next(*this, custom) : ec;
}

auto file_actor_t::operator()(const model::diff::modify::finish_file_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    auto folder = cluster->get_folders().by_id(diff.folder_id);
    auto file_info = folder->get_folder_infos().by_device_id(diff.peer_id);
    auto file = file_info->get_file_infos().by_name(diff.file_name);
    auto local_path = file->get_path();
    auto path = local_path.string();
    auto action = diff.action;

    if (action == model::advance_action_t::resolve_remote_win) {
        auto &self = *cluster->get_device();
        auto local_fi = folder->get_folder_infos().by_device(self);
        auto local_file = local_fi->get_file_infos().by_name(diff.file_name);
        auto conflicting_name = local_file->make_conflicting_name();
        auto target_path = folder->get_path() / conflicting_name;
        auto ec = sys::error_code{};
        path = file->get_path().string();
        LOG_DEBUG(log, "renaming {} -> {}", file->get_name(), conflicting_name);
        bfs::rename(path, target_path);
        if (ec) {
            LOG_ERROR(log, "cannot rename file: {}: {}", path, ec.message());
            return ec;
        }
    }
    auto backend = rw_cache->get(path);
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

    rw_cache->remove(backend);
    auto ok = backend->close(true, local_path);
    if (!ok) {
        auto &ec = ok.assume_error();
        LOG_ERROR(log, "cannot close file: {}: {}", path, ec.message());
        return ec;
    }

    LOG_INFO(log, "file {} ({} bytes) is now locally available", path, file->get_size());

    auto ack = model::diff::advance::advance_t::create(action, *file, *sequencer);
    send<model::payload::model_update_t>(coordinator, std::move(ack), this);
    return diff.visit_next(*this, custom);
}

auto file_actor_t::operator()(const model::diff::modify::append_block_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    auto guard = write_guard_t(*this, diff);
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
    auto r = guard(backend->write(offset, diff.data));
    return r ? diff.visit_next(*this, custom) : r;
}

auto file_actor_t::get_source_for_cloning(model::file_info_ptr_t &source, const file_ptr_t &target_backend) noexcept
    -> outcome::result<file_ptr_t> {
    auto source_path = source->get_path();
    if (source_path == target_backend->get_path()) {
        return target_backend;
    }

    auto source_tmp = make_temporal(source_path);

    if (auto cached = rw_cache->get(source_path.string()); cached) {
        return cached;
    } else if (auto cached = rw_cache->get(source_tmp.string()); cached) {
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
    auto guard = write_guard_t(*this, diff);
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
    auto r = guard(target_backend->copy(target_offset, *source_backend, source_offset, block->get_size()));
    return r ? diff.visit_next(*this, custom) : r;
}

auto file_actor_t::open_file_rw(const std::filesystem::path &path, model::file_info_ptr_t info) noexcept
    -> outcome::result<file_ptr_t> {
    LOG_TRACE(log, "open_file (r/w, by path), path = {}", path.string());
    auto item = rw_cache->get(path.string());
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
    rw_cache->put(ptr);
    return ptr;
}

auto file_actor_t::open_file_ro(const bfs::path &path, bool use_cache) noexcept -> outcome::result<file_ptr_t> {
    LOG_TRACE(log, "open_file (r/o, by path), path = {}", path.string());
    if (use_cache) {
        auto file = rw_cache->get(path.string());
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
