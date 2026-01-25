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
    std::memset(&overlapped, 0, sizeof(overlapped));
    overlapped.hEvent = event_guard.handle;
}

auto watcher_t::path_guard_t::initiate() noexcept -> sys::error_code {
    constexpr auto flags = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_ATTRIBUTES |
                           FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE;
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
    auto &folder_info = folder_map[path_guard->folder_id];
    auto &path_str = folder_info.path_str;

    auto bytes = DWORD{0};
    auto ok = ::GetOverlappedResult(path_guard->dir_guard.handle, &path_guard->overlapped, &bytes, false);
    if (!ok) {
        auto ec = sys::error_code(::GetLastError(), sys::system_category());
        LOG_WARN(log, "cannot get overlapped result for '{}': {}", path_str, ec.message());
        return;
    }

    auto ptr = (FILE_NOTIFY_INFORMATION *)path_guard->buff;
    do {
        auto sz = ptr->FileNameLength / sizeof(WCHAR);
        auto file_wname = std::wstring(ptr->FileName, sz);
        auto file_name = narrow(file_wname);
        LOG_DEBUG(log, "updated({}) '{}'", path_guard->folder_id, file_name);
        ptr = (FILE_NOTIFY_INFORMATION *)(((char *)ptr) + ptr->NextEntryOffset);
    } while (ptr->NextEntryOffset != 0);

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