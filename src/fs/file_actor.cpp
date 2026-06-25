// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2026 Ivan Baidakou

#include "file_actor.h"
#include "fs/platform/context_base.h"
#include "fs_proxy.h"
#include "fs_slave.h"
#include "net/names.h"
#include "utils/path_view.hpp"
#include "utils/path_utils.h"
#include "utils/format.hpp"
#include "utils/platform.h"
#include "utils/error_code.h"
#include "model/messages.h"
#include "syncspirit-config.h"
#include <memory_resource>

using namespace syncspirit::fs;
using namespace syncspirit::proto;

namespace {
namespace resource {
r::plugin::resource_id_t service = 0;
} // namespace resource
namespace to {
struct context {};
} // namespace to
} // namespace

template <> inline auto &rotor::supervisor_t::access<to::context>() noexcept { return context; }

struct file_actor_t::process_context_t : fs_proxy_t {
    using buffer_t = std::array<std::byte, 1024 * 32>;
    using pool_t = std::pmr::monotonic_buffer_resource;
    using allocator_t = std::pmr::polymorphic_allocator<char>;

    process_context_t(const void *cache_key_, file_actor_t &actor)
        : fs_proxy_t(*actor.updates_mediator, clock_t::local_time() + actor.retension), cache_key{cache_key_},
          pool(buffer.data(), buffer.size()), allocator{&pool} {}
    const void *cache_key;

    buffer_t buffer;
    pool_t pool;
    allocator_t allocator;
};

file_actor_t::file_actor_t(config_t &cfg)
    : r::actor_base_t{cfg}, concurrent_hashes{cfg.concurrent_hashes}, retension{cfg.change_retension},
      updates_mediator{cfg.updates_mediator}, scan_dir_callback(cfg.scan_dir_callback),
      watched_folders(cfg.watched_folders) {
    assert(updates_mediator);
    assert(watched_folders);
    if (!retension.is_positive()) {
        LOG_ERROR(log, "retension interval should be positive");
        throw std::runtime_error("retension interval should be positive");
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
                plugin->subscribe_actor(&file_actor_t::on_service_lock, coordinator);
                plugin->subscribe_actor(&file_actor_t::on_service_unlock, coordinator);
            }
        });
        p.discover_name(net::names::db, db, true);
    });
    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
        p.subscribe_actor(&file_actor_t::on_io_signal);
        p.subscribe_actor(&file_actor_t::on_exec);
        p.subscribe_actor(&file_actor_t::on_io_commands);
        p.subscribe_actor(&file_actor_t::on_create_dir);
    });
}

void file_actor_t::on_start() noexcept {
    LOG_TRACE(log, "on_start");
    send<payload::io_signal_t>(address);
    send<model::payload::local_up_t>(coordinator);
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
    if (!io_signal) {
        io_queue.emplace_back(&message);
        return;
    }
    auto &p = message.payload;
    auto ctx = process_context_t(p.context, *this);

    for (auto &cmd : p.commands) {
        static const size_t SS_PATH_MAX = SYNCSPIRIT_PATH_MAX;
        auto buffer = std::array<char, SS_PATH_MAX>();
        auto pool = std::pmr::monotonic_buffer_resource(buffer.data(), buffer.size());
        auto allocator = std::pmr::polymorphic_allocator<std::string>(&pool);
        auto ptr = const_cast<char *>(buffer.data());

        std::visit(
            [&](auto &cmd) {
                ctx.updates_mediator.enable(watched_folders->contains(cmd.folder_id));
                process(cmd, ctx);
            },
            cmd);
    }

    if (ctx.mediator_updates && !expiration_timer) {
        expiration_timer = start_timer(retension, *this, &file_actor_t::on_retension_finish);
    }
    auto sup_ctx = static_cast<platform::context_base_t *>(supervisor->access<to::context>());
    sup_ctx->poll_events();
    supervisor->put(std::move(io_signal));
}

void file_actor_t::on_io_signal(message::io_signal_t &msg) noexcept {
    LOG_TRACE(log, "on_io_signal (queue size: {})", io_queue.size());
    io_signal = &msg;
    if (!io_queue.empty()) {
        auto &io_message = io_queue.front();
        supervisor->put(std::move(io_message));
        io_queue.pop_front();
    }
}

void file_actor_t::on_retension_finish(r::request_id_t, bool cancelled) noexcept {
    LOG_TRACE(log, "on_retension_finish ({} ms)", retension.total_milliseconds());
    expiration_timer.reset();
    if (!cancelled) {
        auto do_respawn = updates_mediator->clean_expired();
        if (do_respawn) {
            expiration_timer = start_timer(retension, *this, &file_actor_t::on_retension_finish);
        }
    }
}

void file_actor_t::on_exec(message::foreign_executor_t &request) noexcept {
    struct execution_ctx_impl_t final : execution_context_t {
        execution_ctx_impl_t(file_actor_t &actor_)
            : actor{&actor_}, fs_proxy_holder(*actor_.updates_mediator, clock_t::local_time() + actor_.retension) {
            plugin = actor->hasher;
            fs_proxy = &fs_proxy_holder;
            scan_dir_callback = actor->scan_dir_callback;
        }

        fs_proxy_t fs_proxy_holder;
        file_actor_t *actor;
    };

    LOG_DEBUG(log, "on_exec");
    auto slave = static_cast<fs::fs_slave_t *>(request.payload.get());
    slave->ec = {};
    auto ctx = execution_ctx_impl_t(*this);
    auto updated = slave->exec(ctx);
    if (updated && !expiration_timer) {
        expiration_timer = start_timer(retension, *this, &file_actor_t::on_retension_finish);
    }
}

void file_actor_t::on_controller_up(net::message::controller_up_t &message) noexcept {
    LOG_DEBUG(log, "on_controller_up, {}", (const void *)message.payload.controller.get());
    resources->acquire(resource::service);
}

void file_actor_t::on_controller_predown(net::message::controller_predown_t &message) noexcept {
    auto &p = message.payload;
    LOG_DEBUG(log, "on_controller_predown, {}, started: {}", (const void *)p.controller.get(), p.started);
    if (p.started) {
        auto cache_key = p.controller.get();
        if (auto it = context_cache.find(cache_key); it != context_cache.end()) {
            context_cache.erase(it);
        }
        resources->release(resource::service);
    }
}

void file_actor_t::on_service_lock(model::message::service_lock_t &message) noexcept {
    if (message.payload.service == net::names::fs_actor) {
        LOG_DEBUG(log, "on_service_lock");
        resources->acquire(resource::service);
    }
}

void file_actor_t::on_service_unlock(model::message::service_unlock_t &message) noexcept {
    if (message.payload.service == net::names::fs_actor) {
        LOG_DEBUG(log, "on_service_unlock");
        resources->release(resource::service);
    }
}

void file_actor_t::process(payload::block_request_t &cmd, process_context_t &context) noexcept {
    LOG_TRACE(log, "processing block request");
    auto path = cmd.path.get_view(context.allocator);
    auto file_opt = open_file_ro(path, context.cache_key);
    auto ec = std::error_code{};
    auto data = utils::bytes_t{};
    if (!file_opt) {
        ec = file_opt.assume_error();
        LOG_ERROR(log, "error opening file {}: {}", path, ec);
        cmd.result = ec;
        return;
    } else {
        auto &file = file_opt.assume_value();
        auto block_opt = file->read(cmd.offset, cmd.block_size);
        if (!block_opt) {
            ec = block_opt.assume_error();
            LOG_WARN(log, "error requesting block; offset = {}, size = {} :: {} ", cmd.offset, cmd.block_size, ec);
            cmd.result = ec;
            return;
        } else {
            data = std::move(block_opt.assume_value());
        }
    }
    cmd.result = std::move(data);
}

void file_actor_t::process(payload::remote_copy_t &cmd, process_context_t &context) noexcept {
    auto path = cmd.path.get_view(context.allocator);
    std::error_code ec;

    if (!cmd.conflict_path.empty()) {
        LOG_DEBUG(log, "renaming {} -> {}", path, cmd.conflict_path);
        auto new_name = cmd.conflict_path.get_view(context.allocator);
        if (auto ec = context.rename(cmd.path, new_name); ec) {
            LOG_ERROR(log, "cannot rename file: {}: {}", path, ec);
            cmd.result = ec;
            return;
        }
    }

    if (cmd.deleted) {
        if (utils::exists(path, ec)) {
            LOG_DEBUG(log, "removing '{}'", path);
            if (auto ec = context.remove(path); ec) {
                LOG_ERROR(log, "error removing {} : {}", path, ec);
                cmd.result = ec;
                return;
            }
        } else {
            LOG_TRACE(log, "{} already abscent, noop", path);
        }
        cmd.result = outcome::success();
        return;
    }

    auto parent = path.get_parent();
    bool set_perms = false;

    bool exists = utils::exists(parent, ec);
    if (!exists) {
        if (auto ec = context.create_directories(parent); ec) {
            cmd.result = ec;
            return;
        }
    }

    if (cmd.type == proto::FileInfoType::FILE) {
        auto sz = cmd.size;
        auto file_opt = context.open_write(path, sz);
        if (file_opt.has_value()) {
            LOG_TRACE(log, "touching existing file '{}' ({} bytes)", path, sz);
        } else {
            auto &ec = file_opt.assume_error();
            LOG_ERROR(log, "error creating '{}': {}", path, ec);
            cmd.result = ec;
            return;
        }
        if (auto ec = context.last_write_time(path, cmd.modification_s); ec) {
            cmd.result = ec;
            return;
        }
        set_perms = !cmd.no_permissions && utils::platform_t::permissions_supported(path);
    } else if (cmd.type == proto::FileInfoType::DIRECTORY) {
        LOG_DEBUG(log, "creating directory '{}'", path);
        if (auto ec = context.create_directories(path); ec) {
            cmd.result = ec;
            return;
        }
        set_perms = !cmd.no_permissions && utils::platform_t::permissions_supported(path);
    } else if (cmd.type == proto::FileInfoType::SYMLINK) {
        if (utils::platform_t::symlinks_supported()) {
            auto target = utils::make_native_view(cmd.symlink_target, context.allocator);
            LOG_DEBUG(log, "creating symlink {} -> {}", path, target);
            bool attempt_create =
                !utils::is_symlink(path, ec) || (std::string_view(utils::read_symlink(path, ec)) != cmd.symlink_target);
            if (attempt_create) {
                if (auto ec = context.create_link(target, path); ec) {
                    LOG_WARN(log, "error symlinking {} -> {} : {}", path, target, ec);
                    cmd.result = ec;
                    return;
                }
            } else {
                LOG_TRACE(log, "no need to create symlink {} -> {}", path, target);
            }
        } else {
            LOG_WARN(log, "symlinks are not supported by platform, no I/O for {}", path);
        }
    }

    if (set_perms) {
        if (auto ec = context.set_perms(path, cmd.permissions); ec) {
            LOG_ERROR(log, "cannot set permissions {:#o} on file: '{}': {}", cmd.permissions, path, ec);
            cmd.result = ec;
            return;
        }
    }
    cmd.result = outcome::success();
}

void file_actor_t::process(payload::finish_file_t &cmd, process_context_t &context) noexcept {
    auto &file_cache = context_cache[context.cache_key];
    auto path = cmd.path.get_view(context.allocator);
    auto it = file_cache.find(cmd.path);
    if (it == file_cache.end()) {
        LOG_DEBUG(log, "attempt to flush non-opened file {}", path);
        auto ec = std::error_code{};
        auto tmp_path = path.make_temporal();
        if (!utils::exists(tmp_path, ec)) {
            cmd.result = utils::make_error_code(utils::error_code_t::flush_non_opened);
            LOG_WARN(log, "file '{}' does not exist", tmp_path);
            return;
        }

        auto option = file_t::open_write(context, path, cmd.file_size);
        if (!option) {
            auto &err = option.assume_error();
            LOG_ERROR(log, "cannot open file '{}': {}", path, err);
            cmd.result = err;
            return;
        }
        auto ptr = file_ptr_t(new file_t(std::move(option.assume_value())));
        it = file_cache.emplace(cmd.path.clone(), ptr).first;
    }

    auto backend = it->second;
    if (!cmd.conflict_path.empty()) {
        auto new_name = cmd.conflict_path.get_view(context.allocator);
        LOG_DEBUG(log, "renaming {} -> {}", path, new_name);
        auto ec = std::error_code();
        if (auto ec = context.rename(cmd.path, new_name); ec) {
            LOG_ERROR(log, "cannot rename file '{}': {}", path, ec);
            cmd.result = ec;
            return;
        }
    }

    file_cache.erase(it);
    auto ok = backend->finalize(&context, cmd.modification_s, path);
    if (!ok) {
        auto &ec = ok.assume_error();
        LOG_ERROR(log, "cannot close file '{}': {}", path, ec);
        cmd.result = ec;
        return;
    }

    if (!cmd.no_permissions) {
        if (auto ec = context.set_perms(path, cmd.permissions); ec) {
            LOG_ERROR(log, "cannot set permissions {:#o} on file: '{}': {}", cmd.permissions, path, ec);
            cmd.result = ec;
            return;
        }
    }

    cmd.result = outcome::success();
    LOG_INFO(log, "file {} ({} bytes) is now locally available", path, cmd.file_size);
}

void file_actor_t::process(payload::append_block_t &cmd, process_context_t &context) noexcept {
    auto path = cmd.path.get_view(context.allocator);
    auto file_opt = open_file_rw(path, cmd.file_size, context);
    if (!file_opt) {
        auto &err = file_opt.assume_error();
        LOG_ERROR(log, "cannot open file: {}: {}", path, err);
        cmd.result = err;
        return;
    }
    auto &backend = file_opt.assume_value();
    cmd.result = backend->write(context, cmd.offset, cmd.data);
}

void file_actor_t::process(payload::clone_block_t &cmd, process_context_t &context) noexcept {
    auto target_path = cmd.path.get_view(context.allocator);
    auto source_path = cmd.source.get_view(context.allocator);
    auto target_opt = open_file_rw(target_path, cmd.target_size, context);
    if (!target_opt) {
        auto &err = target_opt.assume_error();
        LOG_ERROR(log, "cannot open file: {}: {}", target_path, err);
        cmd.result = err;
        return;
    }
    auto target_backend = std::move(target_opt.assume_value());
    auto &file_cache = context_cache[context.cache_key];
    auto source_backend_opt = [&]() -> outcome::result<file_ptr_t> {
        auto it = file_cache.find(cmd.source);
        if (it != file_cache.end()) {
            return it->second;
        } else {
            return open_file_ro(source_path, {});
        }
    }();
    if (!source_backend_opt) {
        auto ec = source_backend_opt.assume_error();
        LOG_ERROR(log, "cannot open source file for cloning: {}: {}", source_path, ec);
        cmd.result = ec;
        return;
    }
    auto &source_backend = *source_backend_opt.assume_value();
    cmd.result = target_backend->copy(context, cmd.target_offset, source_backend, cmd.source_offset, cmd.block_size);
}

void file_actor_t::process(payload::update_meta_t &cmd, process_context_t &context) noexcept {

    auto r = std::error_code();
    auto path = cmd.path.get_view(context.allocator);
    LOG_DEBUG(log, "Updating metadata of '{}'", path);

    if (!cmd.no_permissions && utils::platform_t::permissions_supported(path)) {
        r = context.set_perms(path, cmd.permissions);
    }
    if (!r) {
        r = context.last_write_time(path, cmd.modification_s);
    }

    if (r) {
        LOG_ERROR(log, "cannot update metadata of '{}': {}", path, r);
    }
    cmd.result = r;
}

auto file_actor_t::open_file_rw(const utils::poly_path_view_t &path, std::uint64_t file_size,
                                process_context_t &context) noexcept -> outcome::result<file_ptr_t> {
    auto &file_cache = context_cache[context.cache_key];
    auto it = file_cache.find(path);
    if (it != file_cache.end()) {
        return it->second;
    }

    auto parent = path.get_parent();
    std::error_code ec;

    bool exists = utils::exists(parent, ec);
    if (!exists) {
        utils::create_directories(parent, ec);
        if (ec) {
            return ec;
        }
    }

    auto option = file_t::open_write(context, path, file_size);
    if (!option) {
        return option.assume_error();
    }
    auto ptr = file_ptr_t(new file_t(std::move(option.assume_value())));
    file_cache.emplace(path.detach(), ptr);
    LOG_TRACE(log, "open_file (rw), path = {}, size = {}, cache sz: {}", path, file_size, file_cache.size());
    return ptr;
}

auto file_actor_t::open_file_ro(const utils::poly_path_view_t &path, const void *context) noexcept
    -> outcome::result<file_ptr_t> {
    if (context) {
        auto &file_cache = context_cache[context];
        auto it = file_cache.find(path);
        if (it != file_cache.end()) {
            LOG_TRACE(log, "open_file (r/o, by path, cache hit), path = {}", path);
            return it->second;
        }
    }

    auto opt = file_t::open_read(path);
    if (!opt) {
        return opt.assume_error();
    }
    LOG_TRACE(log, "open_file (r/o, by path), path = {}", path);
    return file_ptr_t(new file_t(std::move(opt.assume_value())));
}

void file_actor_t::on_create_dir(message::create_dir_t &message) noexcept {
    // no need to use updates mediator, as it is never watched and used only
    // for folder creation

    auto buffer = std::array<std::byte, 1024 * 32>();
    auto pool = std::pmr::monotonic_buffer_resource(buffer.data(), buffer.size());
    auto allocator = std::pmr::polymorphic_allocator<char>(&pool);

    auto &p = message.payload;
    auto path = p.get_view(allocator);
    LOG_TRACE(log, "on_create_dir, '{}'", path);
    utils::create_directories(path, p.ec);
}
