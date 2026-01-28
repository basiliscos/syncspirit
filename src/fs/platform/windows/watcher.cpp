// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "watcher.h"

#if SYNCSPIRIT_WATCHER_WIN32

#include "fs/fs_supervisor.h"
#include <boost/nowide/convert.hpp>
#include <cstring>
#include <string>

using namespace syncspirit::fs::platform::windows;
using boost::nowide::narrow;

watcher_t::path_guard_t::path_guard_t(std::string folder_id_, io_guard_t dir_guard_, io_guard_t event_guard_) noexcept
    : folder_id{std::move(folder_id_)}, dir_guard(std::move(dir_guard_)), event_guard(std::move(event_guard_)) {
    overlapped.hEvent = event_guard.handle;
}

auto watcher_t::path_guard_t::initiate() noexcept -> sys::error_code {
    constexpr auto flags = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_ATTRIBUTES |
                           FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE;
    overlapped.Offset = overlapped.OffsetHigh = 0;
    auto ok = ::ReadDirectoryChangesW(dir_guard.handle, buff, BUFF_SZ, true, flags, nullptr, &overlapped, nullptr);
    if (!ok) {
        return sys::error_code(::GetLastError(), sys::system_category());
    }
    return {};
}

static void notify_cb(HANDLE handle, void *data) {
    auto actor = reinterpret_cast<watcher_t *>(data);
    actor->on_notify(handle);
}

void watcher_t::on_watch(message::watch_folder_t &message) noexcept {
    auto sup = static_cast<fs::fs_supervisor_t *>(supervisor);
    auto ctx = static_cast<fs::fs_context_t *>(sup->context);
    auto &p = message.payload;
    auto &path_native = p.path.native();
    auto path_str = narrow(path_native);
    LOG_TRACE(log, "on watch on '{}'", path_str);

    auto dir_handle = ::CreateFileW(path_native.c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                    FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);

    if (dir_handle == INVALID_HANDLE_VALUE) {
        auto ec = sys::error_code(::GetLastError(), sys::system_category());
        LOG_ERROR(log, "cannot open directory '{}' handle: {}", path_str, ec.message());
        p.ec = ec;
        return;
    }
    auto dir_guard = ctx->guard_handle(dir_handle);

    auto event_handle = ::CreateEvent(nullptr, true, false, nullptr);
    if (!event_handle) {
        auto ec = sys::error_code(::GetLastError(), sys::system_category());
        LOG_ERROR(log, "cannot create event handle: {}", path_str, ec.message());
        p.ec = ec;
        return;
    }
    auto event_guard = ctx->register_callback(event_handle, notify_cb, this);

    LOG_TRACE(log, "dir handle = {}, event_handle = {}", (void *)dir_handle, (void *)event_handle);

    auto path_guard = path_guard_ptr_t();
    path_guard.reset(new path_guard_t(std::string(p.folder_id), std::move(dir_guard), std::move(event_guard)));

    if (auto ec = path_guard->initiate(); ec) {
        LOG_ERROR(log, "cannot initate watching dir '{}': {}", path_str, ec.message());
        p.ec = ec;
        return;
    }

    auto folder_info = folder_info_t(p.path, path_str);
    auto [it, inserted] = folder_map.emplace(std::make_pair(std::string(p.folder_id), std::move(folder_info)));
    if (!inserted) {
        LOG_WARN(log, "folder '{}' on '{}' is already watched", p.folder_id, path_str);
    } else {
        path_map[event_handle] = std::move(path_guard);
        p.ec = {};
    }
}

void watcher_t::on_notify(handle_t handle) noexcept {
    LOG_TRACE(log, "on_notify, handle = {}", (void *)handle);
    auto it = path_map.find(handle);
    if (it == path_map.end()) {
        LOG_CRITICAL(log, "cannot find path guard for handle {}", (void *)handle);
        return;
    }
    auto &path_guard = it->second;
    auto &folder_id = path_guard->folder_id;
    auto &folder_info = folder_map[folder_id];
    auto &path_str = folder_info.path_str;

    auto bytes = DWORD{0};
    auto ok = ::GetOverlappedResult(path_guard->dir_guard.handle, &path_guard->overlapped, &bytes, false);
    if (!ok) {
        auto ec = sys::error_code(::GetLastError(), sys::system_category());
        LOG_WARN(log, "cannot get overlapped result for '{}': {}", path_str, ec.message());
        return;
    }
    if (!bytes) {
        LOG_WARN(log, "overflow occured in ReadDirectoryChangesW()");
        return;
    }

    char storage[32 * 1024 * sizeof(wchar_t) + 1];
    auto deadline = clock_t::local_time() + retension;

    auto ptr = (FILE_NOTIFY_INFORMATION *)path_guard->buff;
    auto prev_name = std::string();
    while (ptr) {
        auto storage_ptr = storage;
        auto sz = ptr->FileNameLength / sizeof(WCHAR);
        auto namew_ptr = ptr->FileName;
        for (auto p = namew_ptr, e = namew_ptr + sz + 1; p != e; ++p) {
            if (*p == L'\\') {
                *p = '/';
            }
        }
        auto name_holder = std::string();
        auto name_view = std::string_view();
        if (narrow(storage_ptr, sizeof(storage), namew_ptr, namew_ptr + sz)) {
            name_view = std::string_view(storage_ptr);
        } else {
            auto file_wname = std::wstring_view(ptr->FileName, sz);
            name_holder = narrow(file_wname);
            name_view = path_str;
        }

        auto type = update_type_internal_t{0};
        if (ptr->Action == FILE_ACTION_ADDED) {
            type = update_type::CREATED;
        } else if (ptr->Action == FILE_ACTION_REMOVED) {
            type = update_type::DELETED;
        } else if (ptr->Action == FILE_ACTION_MODIFIED) {
            // no idea how to track metadata changes only
            type = update_type::CONTENT;
        } else if (ptr->Action == FILE_ACTION_RENAMED_OLD_NAME) {
            prev_name = name_holder.empty() ? std::string(name_view) : std::move(name_holder);
        } else if (ptr->Action == FILE_ACTION_RENAMED_NEW_NAME) {
            type = update_type::META;
        }

        if (type) {
            push(deadline, folder_id, name_view, std::move(prev_name), static_cast<update_type_t>(type));
        } else {
            LOG_DEBUG(log, "in the folder '{}' updated ({:x}): '{}'", folder_id, ptr->Action, name_view);
        }
        ptr = ptr->NextEntryOffset ? (FILE_NOTIFY_INFORMATION *)(((char *)ptr) + ptr->NextEntryOffset) : nullptr;
    };

    if (auto ok = ::ResetEvent(handle); !ok) {
        auto ec = sys::error_code(::GetLastError(), sys::system_category());
        LOG_WARN(log, "cannot reset event for handle for '{}': {}", path_str, ec.message());
        return;
    }

    if (auto ec = path_guard->initiate(); ec) {
        LOG_ERROR(log, "cannot initate watching dir '{}': {}", path_str, ec.message());
        return;
    }
}

#endif