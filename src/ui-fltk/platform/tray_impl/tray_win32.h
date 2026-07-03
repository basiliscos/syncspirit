// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#pragma once

#include "syncspirit-fltk-config.h"

#if defined(SYNCSPIRIT_FLTK_WIN32)

#include "platform/tray_base.h"
#include "app_supervisor.h" // for proper headers order

#include <windows.h>
#include <shellapi.h>

namespace syncspirit::fltk {
struct tray_win32_t final : tray_impl_t {
    static tray_win32_t *init(app_supervisor_t &) noexcept;

    tray_win32_t(app_supervisor_t &sup, HICON icon, UINT tray_message);
    tray_win32_t(const tray_win32_t &) = delete;
    tray_win32_t(tray_win32_t &&) = delete;

    ~tray_win32_t();

    bool is_enabled() noexcept override;

    app_supervisor_t &sup;
    HWND handle{nullptr};
    HICON icon{nullptr};
    UINT tray_message{0};
    WNDPROC parent_proc;
    NOTIFYICONDATAW notify_data;
    bool valid{false};
    bool property{false};
    bool shown{false};
};
} // namespace syncspirit::fltk

#endif