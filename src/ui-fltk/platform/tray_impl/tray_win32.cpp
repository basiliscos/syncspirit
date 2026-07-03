// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "syncspirit-fltk-config.h"

#if defined(SYNCSPIRIT_FLTK_WIN32)

#include "tray_win32.h"
#include "main_window.h"
#include "utils/format.hpp"

#include <FL/platform.H>
#include <cstring>

using namespace syncspirit::fltk;

static constexpr wchar_t tray_property_str[] = L"syncspirit.tray.prop";
static constexpr wchar_t tray_message_str[] = L"syncspirit.tray.message";

static LRESULT CALLBACK tray_proc(HWND handle, UINT message, WPARAM wParam, LPARAM lParam) {
    auto tray = reinterpret_cast<tray_win32_t *>(GetPropW(handle, tray_property_str));
    if (tray) {

        return CallWindowProcW(tray->parent_proc, handle, message, wParam, lParam);
    }
    return DefWindowProcW(handle, message, wParam, lParam);
}

tray_win32_t *tray_win32_t::init(app_supervisor_t &sup) noexcept {
    auto main_window = sup.get_main_window();
    auto handle = reinterpret_cast<HWND>(fl_xid(main_window));
    if (!handle) {
        return nullptr;
    }

    auto &log = sup.get_logger();
    auto icon = reinterpret_cast<HICON>(SendMessageW(handle, WM_GETICON, ICON_SMALL, 0));
    if (!icon) {
        auto ec = std::error_code(::GetLastError(), std::system_category());
        LOG_WARN(log, "cannot get icon via SendMessage: {}", ec);
        icon = reinterpret_cast<HICON>(GetClassLongPtrW(handle, GCLP_HICONSM));
    }
    if (!icon) {
        auto ec = std::error_code(::GetLastError(), std::system_category());
        LOG_WARN(log, "cannot get icon via GetClassLongPtr: {}", ec);
        icon = LoadIcon(nullptr, IDI_APPLICATION);
    }
    if (!icon) {
        return nullptr;
    }

    auto tray_message = ::RegisterWindowMessageW(tray_message_str);
    if (!tray_message) {
        auto ec = std::error_code(::GetLastError(), std::system_category());
        LOG_WARN(log, "cannot get icon via RegisterWindowMessage: {}", ec);
        return nullptr;
    }

    auto ptr = new tray_win32_t(sup, icon, tray_message);
    if (ptr->valid) {
        return ptr;
    }

    delete ptr;
    return nullptr;
}

tray_win32_t::tray_win32_t(app_supervisor_t &sup_, HICON icon_, UINT tray_message_)
    : sup{sup_}, icon{CopyIcon(icon_)}, tray_message{tray_message_} {
    std::memset(&notify_data, 0, sizeof(notify_data));

    auto main_window = sup.get_main_window();
    auto &log = sup.get_logger();
    handle = reinterpret_cast<HWND>(fl_xid(main_window));

    auto pp = SetWindowLongPtrW(handle, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&tray_proc));
    if (!pp) {
        auto ec = std::error_code(::GetLastError(), std::system_category());
        LOG_WARN(log, "cannot SetWindowLongPtr: {}", ec);
        return;
    }
    parent_proc = reinterpret_cast<WNDPROC>(pp);

    notify_data.cbSize = sizeof(notify_data);
    notify_data.hWnd = handle;
    notify_data.uID = 1;
    notify_data.uFlags = NIF_MESSAGE | NIF_ICON;
    notify_data.uCallbackMessage = tray_message;
    notify_data.hIcon = icon;

    property = SetPropW(handle, tray_property_str, this);
    shown = Shell_NotifyIconW(NIM_ADD, &notify_data);
    if (shown) {
        valid = true;
    }
}

tray_win32_t::~tray_win32_t() {
    if (icon) {
        DestroyIcon(icon);
    }
    if (parent_proc) {
        SetWindowLongPtrW(handle, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(parent_proc));
    }
    if (property) {
        RemovePropW(handle, tray_property_str);
    }
}

bool tray_win32_t::is_enabled() noexcept { return valid && shown; }

#endif