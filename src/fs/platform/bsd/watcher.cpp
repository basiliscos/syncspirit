#include "watcher.h"

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "watcher.h"

#if SYNCSPIRIT_WATCHER_KQUEUE

#include "fs/fs_supervisor.h"
#include "fs/task/scan_dir.h"
#include <fcntl.h>

using namespace syncspirit::fs::platform::bsd;

namespace {

// NOTE_DELETE & NOTE_RENAME are catch by NOTE_WRITE of parent dir .. and trigger dir rescan

namespace dir {
static constexpr auto FILTER = EVFILT_VNODE;
static constexpr auto FILTER_FLAGS = NOTE_WRITE | NOTE_ATTRIB | NOTE_EXTEND;
} // namespace dir

namespace regular {
static constexpr auto FILTER = EVFILT_VNODE;
static constexpr auto FILTER_FLAGS = NOTE_WRITE | NOTE_ATTRIB;
} // namespace regular

} // namespace

static void node_cb(int fd, void *data, std::uint32_t flags, const pt::ptime &now) {
    auto watcher = reinterpret_cast<watcher_t *>(data);
    watcher->kqueue_callback(fd, flags, now);
}

void watcher_t::do_initialize(r::system_context_t *ctx) noexcept {
    ready = true;
    parent_t::do_initialize(ctx);
}

auto watcher_t::watch_path(std::string_view path, file_type_t type) noexcept -> std::optional<int> {
    static constexpr auto FLAGS = EV_ADD | EV_CLEAR;

    auto r = std::optional<int>{};
    short filter;
    u_int fflags;

    if (type == file_type_t::directory) {
        r = open(path.data(), O_RDONLY);
        filter = dir::FILTER;
        fflags = dir::FILTER_FLAGS;
    } else if (type == file_type_t::regular) {
        r = open(path.data(), O_RDONLY);
        filter = regular::FILTER;
        fflags = regular::FILTER_FLAGS;
    }

    if (!r || *r < 0) {
        return r;
    }

    auto sup = static_cast<fs::fs_supervisor_t *>(supervisor);
    auto ctx = static_cast<platform_context_t *>(sup->context);
    auto ok = ctx->backend.watch(*r, node_cb, this, filter, FLAGS, fflags);
    if (!ok) {
        close(*r);
        return -1;
    }
    return r;
}

auto watcher_t::unwatch_path(int wd, file_type_t type) noexcept -> sys::error_code {
    static constexpr auto FLAGS = EV_DELETE;

    short filter{0};
    u_int fflags{0};

    if (type == file_type_t::directory) {
        filter = dir::FILTER;
        fflags = dir::FILTER_FLAGS;
    } else if (type == file_type_t::regular) {
        filter = regular::FILTER;
        fflags = regular::FILTER_FLAGS;
    }

    if (filter) {
        auto sup = static_cast<fs::fs_supervisor_t *>(supervisor);
        auto ctx = static_cast<platform_context_t *>(sup->context);
        ctx->backend.unwatch(wd, filter, FLAGS, fflags);
        if (close(wd) == -1) {
            return sys::error_code{errno, sys::system_category()};
        }
    }

    return {};
}

void watcher_t::notify(const fs::task::scan_dir_t &scan_dir) noexcept {
    auto path = std::string_view(scan_dir.path.native());
    if (path_to_wd.find(path) != path_to_wd.end()) {
        return parent_t::notify(scan_dir);
    }
    auto pos = path.rfind('/');
    if (pos != std::string::npos && path.size() > 2) {
        auto parent_path = path.substr(0, pos);
        auto it = path_to_wd.find(parent_path);
        if (it != path_to_wd.end()) {
            auto &parent_wd = it->second;
            auto &parent_guard = path_map[parent_wd];
            auto &folder_id = parent_guard.folder_id;
            auto opt = parent_t::watch_path(path, folder_id, file_type_t::directory, parent_wd);
            if (!opt) {
                return;
            }
            auto &[_, fd] = *opt;
            if (fd < 0) {
                LOG_ERROR(log, "cannot watch {}: {}", path, strerror(errno));
            } else {
                parent_t::notify(scan_dir);
            }
        } else {
            LOG_WARN(log, "notification upon non-watched parent of '{}'", path);
        }
    } else {
        LOG_TRACE(log, "(notify) cannot find parent path for '{}'", path);
    }
}

void watcher_t::kqueue_callback(int wd, std::uint32_t flags, const pt::ptime &now) noexcept {
    auto &guard = path_map[wd];
    auto &folder_id = guard.folder_id;
    auto full_path = std::string_view(guard.path);
    auto it_folder = watched_folders->find(folder_id);
    assert(it_folder != watched_folders->end());
    auto &folder_info = it_folder->second;
    auto folder_path = std::string_view(folder_info.path_str);
    auto is_regular = guard.file_type == file_type_t::regular;
    auto rel_path = full_path.size() > folder_path.size() ? full_path.substr(folder_path.size() + 1) : "";
    LOG_TRACE(log, "kqueue_callback ({}), fd: {} ({:#x}), {}", folder_id, wd, flags, rel_path);

    auto type = update_type_internal_t{0};
    if (is_regular) {
        if (flags & NOTE_WRITE) {
            type = update_type::CONTENT;
        } else {
            type = update_type::META;
        }
    } else {
        if (flags & NOTE_ATTRIB) {
            type = update_type::META;
        }
        if (flags & NOTE_DELETE) {
            type = update_type::DELETED;
        }
        if (flags & NOTE_WRITE) {
            type = update_type::CONTENT;
        }
        if (!type) {
            type = update_type::CONTENT;
        }
    }

    auto deadline = now + retension;
    push(deadline, folder_id, rel_path, {}, static_cast<update_type_t>(type));
}

#endif