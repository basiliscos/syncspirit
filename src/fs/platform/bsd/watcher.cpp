#include "watcher.h"

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "watcher.h"

#if SYNCSPIRIT_WATCHER_KQUEUE

#include "fs/fs_supervisor.h"
#include "fs/task/scan_dir.h"
#include "fs/utils.h"
#include "utils/utf8.h"
#include <fcntl.h>
#include <limits.h>
#include <memory_resource>

using namespace syncspirit::fs::platform::bsd;

static constexpr auto FILTER = EVFILT_VNODE;
static constexpr auto FILTER_FLAGS = NOTE_WRITE | NOTE_ATTRIB | NOTE_EXTEND | NOTE_RENAME | NOTE_DELETE;

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
    if (!utils::is_utf8_valid(path)) {
        return {};
    }

    if (type == file_type_t::directory) {
        r = open(path.data(), O_RDONLY);
    } else if (type == file_type_t::regular) {
        if (!is_temporal(path)) {
            r = open(path.data(), O_RDONLY);
        }
    }

    if (!r || *r < 0) {
        return r;
    }

    auto sup = static_cast<fs::fs_supervisor_t *>(supervisor);
    auto ctx = static_cast<platform_context_t *>(sup->context);
    auto ok = ctx->backend.watch(*r, node_cb, this, FILTER, FLAGS, FILTER_FLAGS);
    if (!ok) {
        close(*r);
        return -1;
    }
    return r;
}

auto watcher_t::unwatch_path(int wd, file_type_t type) noexcept -> sys::error_code {
    static constexpr auto FLAGS = EV_DELETE;

    if (!((type == file_type_t::directory) || (type == file_type_t::regular))) {
        return {};
    }

    auto sup = static_cast<fs::fs_supervisor_t *>(supervisor);
    auto ctx = static_cast<platform_context_t *>(sup->context);
    ctx->backend.unwatch(wd, FILTER, FLAGS, FILTER_FLAGS);
    if (close(wd) == -1) {
        return sys::error_code{errno, sys::system_category()};
    }

    return {};
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

    if (!fs::is_temporal(rel_path)) {
        auto type = update_type_internal_t{0};

        if (flags & NOTE_RENAME) {
            auto ec = unwatch_recurse(full_path);
            if (ec) {
                LOG_WARN(log, "cannot unwatch(1) '{}': {}", guard.path, ec.message());
            }
            return;
        } else if (flags & NOTE_DELETE) {
            auto ec = unwatch_wd(wd);
            if (ec) {
                LOG_WARN(log, "cannot unwatch(2) '{}': {}", guard.path, ec.message());
            }
            return;
        } else if (flags & (NOTE_WRITE | NOTE_EXTEND)) {
            type = update_type::CONTENT;
        } else if (flags & NOTE_ATTRIB) {
            type = update_type::META;
        }

        if (!type && is_regular) {
            type = update_type::CONTENT;
        }
        if (!type) {
            LOG_TRACE(log, "unexpected kqueue_callback ({}), fd: {} ({:#x}), {}", folder_id, wd, flags, rel_path);
            return;
        }

        auto deadline = now + retension;
        auto requires_refinement = !is_regular && type == update_type::CONTENT;
        push(deadline, folder_id, rel_path, {}, static_cast<update_type_t>(type), requires_refinement);
    }
}

#endif