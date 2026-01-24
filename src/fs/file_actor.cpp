// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2026 Ivan Baidakou

#include "file_actor.h"
#include "fs/fs_slave.h"
#include "net/names.h"
#include "utils.h"
#include "utils/io.h"
#include "utils/format.hpp"
#include "utils/platform.h"
#include "utils/error_code.h"
#include "proto/proto-helpers-bep.h"
#include <boost/nowide/convert.hpp>
#include <memory_resource>

using namespace syncspirit::fs;
using namespace syncspirit::proto;
using boost::nowide::narrow;

namespace {
namespace resource {
r::plugin::resource_id_t controller = 0;
} // namespace resource
} // namespace

file_actor_t::file_actor_t(config_t &cfg)
    : r::actor_base_t{cfg}, concurrent_hashes{cfg.concurrent_hashes}, retension{cfg.change_retension},
      updates_mediator{cfg.updates_mediator} {
    if (updates_mediator) {
        if (!retension.is_positive()) {
            LOG_ERROR(log, "retension interval should be positive");
            throw std::runtime_error("retension interval should be positive");
        }
    }
}

void file_actor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) {
        p.set_identity(net::names::fs_actor, false);
        log = utils::get_logger(identity);
    });
    plugin.with_casted<hasher::hasher_plugin_t>([&](auto &p) {
        hasher = &p;
        p.configure_hashers(concurrent_hashes);
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
        p.subscribe_actor(&file_actor_t::on_exec);
        p.subscribe_actor(&file_actor_t::on_io_commands);
        p.subscribe_actor(&file_actor_t::on_create_dir);
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
    context_cache.clear();
    r::actor_base_t::shutdown_finish();
}

void file_actor_t::on_io_commands(message::io_commands_t &message) noexcept {
    using clock_t = pt::microsec_clock;
    auto &p = message.payload;
    auto deadline = updates_mediator ? clock_t::local_time() : pt::ptime();
    auto updates_sz = int{0};

    for (auto &cmd : p.commands) {
        static const size_t SS_PATH_MAX = 32 * 1024;
        auto buffer = std::array<char, SS_PATH_MAX>();
        auto pool = std::pmr::monotonic_buffer_resource(buffer.data(), buffer.size());
        auto allocator = std::pmr::polymorphic_allocator<std::string>(&pool);
        auto ptr = const_cast<char *>(buffer.data());

        std::visit(
            [&](auto &cmd) {
                auto path_wstr = cmd.path.generic_wstring();
                auto path_wstr_ptr = path_wstr.data();
                auto path_str = std::string();
                auto path_view = std::string_view();
                if (narrow(ptr, buffer.size(), path_wstr_ptr, path_wstr_ptr + path_wstr.size())) {
                    path_view = std::string_view(ptr);
                } else {
                    path_str = narrow(path_wstr);
                    path_view = path_str;
                }
                process(cmd, path_view, p.context);
                if (updates_mediator) {
                    updates_mediator->push(std::string(path_view), deadline);
                    ++updates_sz;
                }
            },
            cmd);
    }

    if (updates_sz && !expiration_timer) {
        expiration_timer = start_timer(retension, *this, &file_actor_t::on_retension_finish);
    }
}

void file_actor_t::on_retension_finish(r::request_id_t, bool cancelled) noexcept {
    LOG_TRACE(log, "on_retension_finish");
    expiration_timer.reset();
    if (!cancelled) {
        auto do_respawn = updates_mediator->clean_expired();
        if (do_respawn) {
            expiration_timer = start_timer(retension, *this, &file_actor_t::on_retension_finish);
        }
    }
}

void file_actor_t::on_exec(message::foreign_executor_t &request) noexcept {
    LOG_DEBUG(log, "on_exec");
    auto slave = static_cast<fs::fs_slave_t *>(request.payload.get());
    slave->ec = {};
    slave->exec(hasher);
}

void file_actor_t::on_controller_up(net::message::controller_up_t &message) noexcept {
    LOG_DEBUG(log, "on_controller_up, {}", (const void *)message.payload.controller.get());
    resources->acquire(resource::controller);
}

void file_actor_t::on_controller_predown(net::message::controller_predown_t &message) noexcept {
    auto &p = message.payload;
    LOG_DEBUG(log, "on_controller_predown, {}, started: {}", (const void *)p.controller.get(), p.started);
    if (p.started) {
        auto cache_key = p.controller.get();
        if (auto it = context_cache.find(cache_key); it != context_cache.end()) {
            context_cache.erase(it);
        }
        resources->release(resource::controller);
    }
}

void file_actor_t::process(payload::block_request_t &cmd, std::string_view path_str, const void *context) noexcept {
    LOG_TRACE(log, "processing block request");
    auto &path = cmd.path;
    auto file_opt = open_file_ro(path, context);
    auto ec = sys::error_code{};
    auto data = utils::bytes_t{};
    if (!file_opt) {
        ec = file_opt.assume_error();
        LOG_ERROR(log, "error opening file {}: {}", path_str, ec.message());
        cmd.result = ec;
        return;
    } else {
        auto &file = file_opt.assume_value();
        auto block_opt = file->read(cmd.offset, cmd.block_size);
        if (!block_opt) {
            ec = block_opt.assume_error();
            LOG_WARN(log, "error requesting block; offset = {}, size = {} :: {} ", cmd.offset, cmd.block_size,
                     ec.message());
            cmd.result = ec;
            return;
        } else {
            data = std::move(block_opt.assume_value());
        }
    }
    cmd.result = std::move(data);
}

void file_actor_t::process(payload::remote_copy_t &cmd, std::string_view path_str, const void *context) noexcept {
    auto &path = cmd.path;
    sys::error_code ec;

    if (!cmd.conflict_path.empty()) {
        auto conflict_path_str = cmd.conflict_path.generic_string();
        LOG_DEBUG(log, "renaming {} -> {}", path_str, conflict_path_str);
        auto ec = sys::error_code();
        bfs::rename(cmd.path, cmd.conflict_path);
        if (ec) {
            LOG_ERROR(log, "cannot rename file: {}: {}", path_str, ec.message());
            cmd.result = ec;
            return;
        }
    }

    if (cmd.deleted) {
        if (bfs::exists(path, ec)) {
            LOG_DEBUG(log, "removing {}", path_str);
            auto ok = bfs::remove_all(path, ec);
            if (!ok) {
                LOG_ERROR(log, "error removing {} : {}", path_str, ec.message());
                cmd.result = ec;
                return;
            }
        } else {
            LOG_TRACE(log, "{} already abscent, noop", path_str);
        }
        cmd.result = outcome::success();
        return;
    }

    auto parent = path.parent_path();
    bool set_perms = false;

    bool exists = bfs::exists(parent, ec);
    if (!exists) {
        bfs::create_directories(parent, ec);
        if (ec) {
            cmd.result = ec;
            return;
        }
    }

    if (cmd.type == proto::FileInfoType::FILE) {
        auto sz = cmd.size;
        bool temporal = sz > 0;
        if (temporal) {
            LOG_TRACE(log, "touching existing file {} ({} bytes)", path.string(), sz);
        } else {
            LOG_TRACE(log, "touching empty file {}", path.string());
            auto out = utils::ofstream_t(path, utils::ofstream_t::trunc);
            if (!out) {
                auto ec = sys::error_code{errno, sys::system_category()};
                LOG_ERROR(log, "error creating {}: {}", path.string(), ec.message());
                cmd.result = ec;
                return;
            }
            out.close();
        }
        bfs::last_write_time(path, from_unix(cmd.modification_s), ec);
        if (ec) {
            cmd.result = ec;
            return;
        }
        set_perms = !cmd.no_permissions && utils::platform_t::permissions_supported(path);
    } else if (cmd.type == proto::FileInfoType::DIRECTORY) {
        LOG_DEBUG(log, "creating directory {}", path.string());
        bfs::create_directory(path, ec);
        if (ec) {
            cmd.result = ec;
            return;
        }
        set_perms = !cmd.no_permissions && utils::platform_t::permissions_supported(path);
    } else if (cmd.type == proto::FileInfoType::SYMLINK) {
        if (utils::platform_t::symlinks_supported()) {
            auto target = bfs::path(cmd.symlink_target);
            LOG_DEBUG(log, "creating symlink {} -> {}", path.string(), target.string());

            bool attempt_create =
                !bfs::exists(path, ec) || !bfs::is_symlink(path, ec) || (bfs::read_symlink(path, ec) != target);
            if (attempt_create) {
                bfs::create_symlink(target, path, ec);
                if (ec) {
                    LOG_WARN(log, "error symlinking {} -> {} : {}", path.string(), target.string(), ec.message());
                    cmd.result = ec;
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
        auto perms = static_cast<bfs::perms>(cmd.permissions);
        bfs::permissions(path, perms, ec);
        if (ec) {
            LOG_ERROR(log, "cannot set permissions {:#o} on file: '{}': {}", cmd.permissions, path.string(),
                      ec.message());
            cmd.result = ec;
            return;
        }
    }
    cmd.result = outcome::success();
}

void file_actor_t::process(payload::finish_file_t &cmd, std::string_view path_str, const void *context) noexcept {
    auto &file_cache = context_cache[context];
    auto it = file_cache.find(cmd.path);
    if (it == file_cache.end()) {
        LOG_DEBUG(log, "attempt to flush non-opened file {}", path_str);
        auto ec = sys::error_code{};
        auto tmp_path = make_temporal(cmd.path);
        if (!bfs::exists(tmp_path, ec)) {
            cmd.result = utils::make_error_code(utils::error_code_t::flush_non_opened);
            LOG_WARN(log, "file '{}' does not exist", tmp_path.generic_string());
            return;
        }

        auto option = file_t::open_write(cmd.path, cmd.file_size);
        if (!option) {
            auto &err = option.assume_error();
            LOG_ERROR(log, "cannot open file '{}': {}", path_str, err.message());
            cmd.result = err;
            return;
        }
        auto ptr = file_ptr_t(new file_t(std::move(option.assume_value())));
        it = file_cache.emplace(cmd.path, ptr).first;
    }

    if (!cmd.conflict_path.empty()) {
        auto conflict_path_str = cmd.conflict_path.generic_string();
        LOG_DEBUG(log, "renaming {} -> {}", path_str, conflict_path_str);
        auto ec = sys::error_code();
        bfs::rename(cmd.path, cmd.conflict_path);
        if (ec) {
            LOG_ERROR(log, "cannot rename file '{}': {}", path_str, ec.message());
            cmd.result = ec;
            return;
        }
    }

    auto backend = it->second;
    file_cache.erase(it);
    auto ok = backend->close(cmd.modification_s, cmd.path);
    if (!ok) {
        auto &ec = ok.assume_error();
        LOG_ERROR(log, "cannot close file '{}': {}", path_str, ec.message());
        cmd.result = ec;
        return;
    }

    if (!cmd.no_permissions) {
        auto perms = static_cast<bfs::perms>(cmd.permissions);
        auto ec = sys::error_code();
        bfs::permissions(cmd.path, perms, ec);
        if (ec) {
            LOG_ERROR(log, "cannot set permissions {:#o} on file: '{}': {}", cmd.permissions, cmd.path.string(),
                      ec.message());
            cmd.result = ec;
            return;
        }
    }

    cmd.result = outcome::success();
    LOG_INFO(log, "file {} ({} bytes) is now locally available", path_str, cmd.file_size);
}

void file_actor_t::process(payload::append_block_t &cmd, std::string_view path_str, const void *context) noexcept {
    auto &path = cmd.path;
    auto file_opt = open_file_rw(path, cmd.file_size, context);
    if (!file_opt) {
        auto &err = file_opt.assume_error();
        LOG_ERROR(log, "cannot open file: {}: {}", path_str, err.message());
        cmd.result = err;
        return;
    }
    auto &backend = file_opt.assume_value();
    cmd.result = backend->write(cmd.offset, cmd.data);
}

void file_actor_t::process(payload::clone_block_t &cmd, std::string_view path_str, const void *context) noexcept {
    auto &target_path = cmd.path;
    auto target_opt = open_file_rw(target_path, cmd.target_size, context);
    if (!target_opt) {
        auto &err = target_opt.assume_error();
        LOG_ERROR(log, "cannot open file: {}: {}", path_str, err.message());
        cmd.result = err;
        return;
    }
    auto target_backend = std::move(target_opt.assume_value());
    auto &file_cache = context_cache[context];
    auto source_backend_opt = [&]() -> outcome::result<file_ptr_t> {
        auto it = file_cache.find(cmd.source);
        if (it != file_cache.end()) {
            return it->second;
        } else {
            return open_file_ro(cmd.source, {});
        }
    }();
    if (!source_backend_opt) {
        auto path_str = cmd.source.string();
        auto ec = source_backend_opt.assume_error();
        LOG_ERROR(log, "cannot open source file for cloning: {}: {}", path_str, ec.message());
        cmd.result = ec;
        return;
    }
    auto &source_backend = *source_backend_opt.assume_value();
    cmd.result = target_backend->copy(cmd.target_offset, source_backend, cmd.source_offset, cmd.block_size);
}

auto file_actor_t::open_file_rw(const std::filesystem::path &path, std::uint64_t file_size,
                                const void *context) noexcept -> outcome::result<file_ptr_t> {
    auto &file_cache = context_cache[context];
    auto it = file_cache.find(path);
    if (it != file_cache.end()) {
        return it->second;
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
    file_cache[path] = ptr;
    return ptr;
}

auto file_actor_t::open_file_ro(const bfs::path &path, const void *context) noexcept -> outcome::result<file_ptr_t> {
    if (context) {
        auto &file_cache = context_cache[context];
        auto it = file_cache.find(path);
        if (it != file_cache.end()) {
            LOG_TRACE(log, "open_file (r/o, by path, cache hit), path = {}", path.string());
            return it->second;
        }
    }

    auto opt = file_t::open_read(path);
    if (!opt) {
        return opt.assume_error();
    }
    LOG_TRACE(log, "open_file (r/o, by path), path = {}", path.string());
    return file_ptr_t(new file_t(std::move(opt.assume_value())));
}

void file_actor_t::on_create_dir(message::create_dir_t &message) noexcept {
    // no need to use updates mediator, as it is never watched and used only
    // for folder creation
    auto &path = message.payload;
    LOG_TRACE(log, "on_create_dir, '{}'", narrow(path.wstring()));
    bfs::create_directories(path, path.ec);
}
