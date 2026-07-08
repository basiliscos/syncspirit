// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#pragma once

#include "syncspirit-fltk-config.h"

#if defined(SYNCSPIRIT_FLTK_WIN32)

#include "platform/tray.h"
#include "app_supervisor.h" // for proper headers order

#include <windows.h>
#include <shellapi.h>

namespace syncspirit::fltk {
struct tray_win32_t final : tray_impl_t {
    static tray_win32_t *init(app_supervisor_t &) noexcept;

    tray_win32_t(app_supervisor_t &sup);
    tray_win32_t(const tray_win32_t &) = delete;
    tray_win32_t(tray_win32_t &&) = delete;

    ~tray_win32_t();

    bool is_enabled() noexcept override;
    void set_default_icon() noexcept override;
    void set_traffic_icon() noexcept override;

    void make_traffic_icon() noexcept;

    HMODULE instance{nullptr};
    bool has_window_class{false};
    HWND handle{nullptr};
    HICON icon_default{nullptr};
    HICON icon_traffic{nullptr};
    UINT tray_message{0};
    WNDPROC parent_proc;
    NOTIFYICONDATAW notify_data;
    bool valid{false};
    bool property{false};
    bool shown{false};
};
} // namespace syncspirit::fltk

#endif
