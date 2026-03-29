#include "watcher.h"

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "watcher.h"

#if SYNCSPIRIT_WATCHER_KQUEUE

#include "fs/fs_supervisor.h"
#include <fcntl.h>

using namespace syncspirit::fs::platform::bsd;

static constexpr auto FILTER_FLAGS = NOTE_WRITE | NOTE_DELETE | NOTE_ATTRIB | NOTE_RENAME | NOTE_EXTEND | NOTE_LINK;

static void node_cb(int fd, void *data, std::uint32_t flags) {
    auto watcher = reinterpret_cast<watcher_t *>(data);
    watcher->kqueue_callback(fd, flags);
}

void watcher_t::do_initialize(r::system_context_t *ctx) noexcept {
    ready = true;
    parent_t::do_initialize(ctx);
}

auto watcher_t::watch_dir(std::string_view path) noexcept -> outcome::result<int> {
    int fd = open(path.data(), O_RDONLY);
    if (fd == -1) {
        return sys::error_code{errno, sys::system_category()};
    }

    auto sup = static_cast<fs::fs_supervisor_t *>(supervisor);
    auto ctx = static_cast<platform_context_t *>(sup->context);
    auto ok = ctx->backend.watch(fd, node_cb, this, EVFILT_VNODE, EV_ADD | EV_CLEAR, FILTER_FLAGS);
    if (!ok) {
        close(fd);
        return sys::error_code{sys::errc::io_error, sys::generic_category()};
    }
    return fd;
}

auto watcher_t::unwatch_dir(int wd) noexcept -> sys::error_code {
    auto sup = static_cast<fs::fs_supervisor_t *>(supervisor);
    auto ctx = static_cast<platform_context_t *>(sup->context);
    ctx->backend.unwatch(wd, EVFILT_VNODE, EV_DELETE, FILTER_FLAGS);
    if (close(wd) == -1) {
        return sys::error_code{errno, sys::system_category()};
    }
    return {};
}

void watcher_t::kqueue_callback(int wd, std::uint32_t flags) noexcept {
    auto &guard = path_map[wd];
    auto &folder_id = guard.folder_id;
    auto full_path = std::string_view(guard.path);
    auto it_folder = watched_folders->find(folder_id);
    assert(it_folder != watched_folders->end());
    auto &folder_info = it_folder->second;
    auto folder_path = std::string_view(folder_info.path_str);
    auto rel_path = full_path.size() > folder_path.size() ? full_path.substr(folder_path.size() + 1) : "";
    LOG_TRACE(log, "kqueue_callback ({}), fd: {} ({:#x}), {}", folder_id, wd, flags, rel_path);

    auto type = update_type_internal_t{0};
    if (flags & NOTE_ATTRIB) {
        type = update_type::META;
    }
    if (flags & NOTE_DELETE) {
        type = update_type::DELETED;
    }
    if (!type) {
        type = update_type::CONTENT;
    }

    auto deadline = clock_t::local_time() + retension;
    push(deadline, folder_id, rel_path, {}, static_cast<update_type_t>(type));
}

#endif