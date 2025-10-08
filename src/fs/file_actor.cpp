// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "file_actor.h"
#include "net/names.h"
#include "utils.h"
#include "utils/io.h"
#include "utils/format.hpp"
#include "utils/platform.h"
#include "utils/error_code.h"
#include "proto/proto-helpers-bep.h"
#include <boost/nowide/convert.hpp>

using namespace syncspirit::fs;
using namespace syncspirit::proto;

namespace {
namespace resource {
r::plugin::resource_id_t controller = 0;
} // namespace resource
} // namespace

file_actor_t::file_actor_t(config_t &cfg)
    : r::actor_base_t{cfg}, rw_cache(std::move(cfg.rw_cache)), ro_cache(rw_cache->get_max_items()) {}

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
                plugin->subscribe_actor(&file_actor_t::on_controller_predown, coordinator);
            }
        });
        p.discover_name(net::names::db, db, true);
    });
    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
        p.subscribe_actor(&file_actor_t::on_block_request);
        p.subscribe_actor(&file_actor_t::on_remote_copy);
        p.subscribe_actor(&file_actor_t::on_finish_file);
        p.subscribe_actor(&file_actor_t::on_append_block);
        p.subscribe_actor(&file_actor_t::on_clone_block);
    });
}

void file_actor_t::on_start() noexcept {
    LOG_TRACE(log, "on_start");
    r::actor_base_t::on_start();
}

void file_actor_t::shutdown_start() noexcept {
    LOG_TRACE(log, "shutdown_start");
    if (coordinator) {
        send<net::payload::fs_predown_t>(coordinator);
    }
    r::actor_base_t::shutdown_start();
}

void file_actor_t::shutdown_finish() noexcept {
    LOG_TRACE(log, "shutdown_finish");
    rw_cache->clear();
    r::actor_base_t::shutdown_finish();
}

void file_actor_t::on_block_request(message::block_request_t &message) noexcept {
    LOG_TRACE(log, "on_block_request");
    auto &p = message.payload;
    auto &path = p.path;
    auto file_opt = open_file_ro(path, true);
    auto ec = sys::error_code{};
    auto data = utils::bytes_t{};
    if (!file_opt) {
        ec = file_opt.assume_error();
        LOG_ERROR(log, "error opening file {}: {}", path.string(), ec.message());
        p.result = ec;
        return;
    } else {
        auto &file = file_opt.assume_value();
        auto block_opt = file->read(p.offset, p.block_size);
        if (!block_opt) {
            ec = block_opt.assume_error();
            LOG_WARN(log, "error requesting block; offset = {}, size = {} :: {} ", p.offset, p.block_size,
                     ec.message());
            p.result = ec;
            return;
        } else {
            data = std::move(block_opt.assume_value());
        }
    }
    p.result = std::move(data);
}

void file_actor_t::on_controller_up(net::message::controller_up_t &message) noexcept {
    LOG_DEBUG(log, "on_controller_up, {}", (const void *)message.payload.controller.get());
    resources->acquire(resource::controller);
}

void file_actor_t::on_controller_predown(net::message::controller_predown_t &message) noexcept {
    auto &p = message.payload;
    LOG_DEBUG(log, "on_controller_predown, {}, started: {}", (const void *)p.controller.get(), p.started);
    if (p.started) {
        resources->release(resource::controller);
    }
}

void file_actor_t::on_remote_copy(message::remote_copy_t &message) noexcept {
    auto &p = message.payload;
    auto &path = p.path;
    sys::error_code ec;

    if (p.deleted) {
        if (bfs::exists(path, ec)) {
            LOG_DEBUG(log, "removing {}", path.string());
            auto ok = bfs::remove_all(path, ec);
            if (!ok) {
                LOG_ERROR(log, "error removing {} : {}", path.string(), ec.message());
                p.result = ec;
                return;
            }
        } else {
            LOG_TRACE(log, "{} already abscent, noop", path.string());
        }
        return;
    }

    auto parent = path.parent_path();
    bool set_perms = false;

    bool exists = bfs::exists(parent, ec);
    if (!exists) {
        bfs::create_directories(parent, ec);
        if (ec) {
            p.result = ec;
            return;
        }
    }

    if (p.type == proto::FileInfoType::FILE) {
        auto sz = p.size;
        bool temporal = sz > 0;
        if (temporal) {
            LOG_TRACE(log, "touching file {} ({} bytes)", path.string(), sz);
            auto file_opt = open_file_rw(path, sz);
            if (!file_opt) {
                auto &err = file_opt.assume_error();
                LOG_ERROR(log, "cannot open file: {}: {}", path.string(), err.message());
                p.result = err;
                return;
            }
            path = file_opt.assume_value()->get_path();
        } else {
            LOG_TRACE(log, "touching empty file {}", path.string());
            auto out = utils::ofstream_t(path, utils::ofstream_t::trunc);
            if (!out) {
                auto ec = sys::error_code{errno, sys::system_category()};
                LOG_ERROR(log, "error creating {}: {}", path.string(), ec.message());
                p.result = ec;
                return;
            }
            out.close();
            bfs::last_write_time(path, from_unix(p.modification_s), ec);
            if (ec) {
                p.result = ec;
                return;
            }
        }
        set_perms = !p.no_permissions && utils::platform_t::permissions_supported(path);
    } else if (p.type == proto::FileInfoType::DIRECTORY) {
        LOG_DEBUG(log, "creating directory {}", path.string());
        bfs::create_directory(path, ec);
        if (ec) {
            p.result = ec;
            return;
        }
        set_perms = !p.no_permissions && utils::platform_t::permissions_supported(path);
    } else if (p.type == proto::FileInfoType::SYMLINK) {
        if (utils::platform_t::symlinks_supported()) {
            auto target = bfs::path(p.symlink_target);
            LOG_DEBUG(log, "creating symlink {} -> {}", path.string(), target.string());

            bool attempt_create =
                !bfs::exists(path, ec) || !bfs::is_symlink(path, ec) || (bfs::read_symlink(path, ec) != target);
            if (attempt_create) {
                bfs::create_symlink(target, path, ec);
                if (ec) {
                    LOG_WARN(log, "error symlinking {} -> {} : {}", path.string(), target.string(), ec.message());
                    p.result = ec;
                    return;
                }
            } else {
                LOG_TRACE(log, "no need to create symlink {} -> {}", path.string(), target.string());
            }
        } else {
            LOG_WARN(log, "symlinks are not supported by platform, no I/O for {}", path.string());
        }
    }

    if (set_perms) {
        auto perms = static_cast<bfs::perms>(p.permissions);
        bfs::permissions(path, perms, ec);
        if (ec) {
            LOG_ERROR(log, "cannot set permissions {:#o} on file: '{}': {}", p.permissions, path.string(),
                      ec.message());
            p.result = ec;
            return;
        }
    }
}

void file_actor_t::on_finish_file(message::finish_file_t &message) noexcept {
    auto &p = message.payload;

    auto path_str = p.path.generic_string();
    auto backend = rw_cache->get(p.path);
    if (!backend) {
        LOG_WARN(log, "attempt to flush non-opened file {}", path_str);
        p.result = utils::make_error_code(utils::error_code_t::nonunique_filename);
        return;
    }

    rw_cache->remove(backend);
    auto ok = backend->close(p.modification_s, p.local_path);
    if (!ok) {
        auto local_path_str = p.local_path.generic_string();
        auto &ec = ok.assume_error();
        LOG_ERROR(log, "cannot close file: {}: {}", local_path_str, ec.message());
        p.result = ec;
        return;
    }

    LOG_INFO(log, "file {} ({} bytes) is now locally available", path_str, p.file_size);
}

void file_actor_t::on_append_block(message::append_block_t &message) noexcept {
    auto &p = message.payload;
    auto &path = p.path;
    auto file_opt = open_file_rw(path, p.file_size);
    if (!file_opt) {
        auto path_str = path.string();
        auto &err = file_opt.assume_error();
        LOG_ERROR(log, "cannot open file: {}: {}", path_str, err.message());
        p.result = err;
        return;
        return;
    }
    auto &backend = file_opt.assume_value();
    p.result = backend->write(p.offset, p.data);
}

void file_actor_t::on_clone_block(message::clone_block_t &message) noexcept {
    auto &p = message.payload;
    auto target_path = p.target;
    auto target_opt = open_file_rw(target_path, p.target_size);
    if (!target_opt) {
        auto path_str = target_path.string();
        auto &err = target_opt.assume_error();
        LOG_ERROR(log, "cannot open file: {}: {}", path_str, err.message());
        p.result = err;
        return;
    }
    auto target_backend = std::move(target_opt.assume_value());
    auto source_backend_opt = [&]() -> outcome::result<file_ptr_t> {
        if (auto cached = rw_cache->get(p.source); cached) {
            return cached;
        } else if (auto cached = ro_cache.get(p.source); cached) {
            return cached;
        } else {
            return open_file_ro(p.source, false);
        }
    }();
    if (!source_backend_opt) {
        auto path_str = p.source.string();
        auto ec = source_backend_opt.assume_error();
        LOG_ERROR(log, "cannot open source file for cloning: {}: {}", path_str, ec.message());
        p.result = ec;
        return;
    }
    auto &source_backend = *source_backend_opt.assume_value();
    p.result = target_backend->copy(p.target_offset, source_backend, p.source_offset, p.block_size);
}

auto file_actor_t::open_file_rw(const std::filesystem::path &path, std::uint64_t file_size) noexcept
    -> outcome::result<file_ptr_t> {
    LOG_TRACE(log, "open_file (r/w, by path), path = {}", path.string());
    auto item = rw_cache->get(path);
    if (item) {
        return item;
    }
    LOG_TRACE(log, "open_file (rw), path = {}, size = {}", path.string(), file_size);

    auto parent = path.parent_path();
    sys::error_code ec;

    bool exists = bfs::exists(parent, ec);
    if (!exists) {
        bfs::create_directories(parent, ec);
        if (ec) {
            return ec;
        }
    }

    auto option = file_t::open_write(path, file_size);
    if (!option) {
        return option.assume_error();
    }
    auto ptr = file_ptr_t(new file_t(std::move(option.assume_value())));
    rw_cache->put(ptr);
    return ptr;
}

auto file_actor_t::open_file_ro(const bfs::path &path, bool use_cache) noexcept -> outcome::result<file_ptr_t> {
    if (use_cache) {
        auto file = rw_cache->get(path);
        if (file) {
            LOG_TRACE(log, "open_file (r/o, by path, cache hit), path = {}", path.string());
            return file;
        }
    }

    auto opt = file_t::open_read(path);
    if (!opt) {
        return opt.assume_error();
    }
    LOG_TRACE(log, "open_file (r/o, by path), path = {}", path.string());
    return file_ptr_t(new file_t(std::move(opt.assume_value())));
}
