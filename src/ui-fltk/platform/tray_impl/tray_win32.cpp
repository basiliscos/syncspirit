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
static constexpr wchar_t tray_window_class_str[] = L"syncspirit.tray.window";

static LRESULT CALLBACK tray_proc(HWND handle, UINT message, WPARAM wParam, LPARAM lParam) {
    auto tray = reinterpret_cast<tray_win32_t *>(GetPropW(handle, tray_property_str));
    if (tray) {
        if (message == tray->tray_message) {
            auto main_window = tray->sup.get_main_window();
            switch (LOWORD(lParam)) {
            case WM_COMMAND:
                return 0;
            case WM_LBUTTONUP:
            case WM_LBUTTONDBLCLK: {
                Fl::awake(
                    [](void *p) {
                        auto main_window = reinterpret_cast<main_window_t *>(p);
                        HWND hwnd = (HWND)fl_xid(main_window);
                        auto is_visible = IsWindowVisible(hwnd) != FALSE;
                        if (is_visible) {
                            ShowWindow(hwnd, SW_HIDE);
                        } else {
                            ShowWindow(hwnd, SW_SHOW);
                            SetForegroundWindow(hwnd);
                            SetActiveWindow(hwnd);
                            main_window->redraw();
                            main_window->flush();
                        }
                    },
                    main_window);
                return 0;
            }
            case WM_RBUTTONUP: {
                auto &log = tray->sup.get_logger();
                HWND hwnd = (HWND)fl_xid(main_window);
                return 0;
            }
            }
        }
    }
    return DefWindowProcW(handle, message, wParam, lParam);
}

tray_win32_t *tray_win32_t::init(app_supervisor_t &sup) noexcept {

    auto ptr = new tray_win32_t(sup);
    if (ptr->valid) {
        return ptr;
    }

    delete ptr;
    return nullptr;
}

tray_win32_t::tray_win32_t(app_supervisor_t &sup_) : sup{sup_} {
    std::memset(&notify_data, 0, sizeof(notify_data));

    auto &log = sup.get_logger();

    instance = GetModuleHandle(nullptr);
    if (!instance) {
        auto ec = std::error_code(::GetLastError(), std::system_category());
        LOG_WARN(log, "cannot GetModuleHandle: {}", ec);
        return;
    }

    WNDCLASSW window_class{};
    window_class.lpfnWndProc = tray_proc;
    window_class.hInstance = instance;
    window_class.lpszClassName = tray_window_class_str;
    if (!RegisterClassW(&window_class)) {
        auto ec = std::error_code(::GetLastError(), std::system_category());
        LOG_WARN(log, "cannot RegisterClass: {}", ec);
        return;
    }
    has_window_class = true;

    handle = CreateWindowExW(0, tray_window_class_str, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, nullptr);
    if (!handle) {
        auto ec = std::error_code(::GetLastError(), std::system_category());
        LOG_WARN(log, "cannot CreateWindow: {}", ec);
        return;
    }

    auto main_window = sup.get_main_window();
    auto main_handle = reinterpret_cast<HWND>(fl_xid(main_window));
    if (!main_handle) {
        return;
    }

    auto icon = reinterpret_cast<HICON>(SendMessageW(main_handle, WM_GETICON, ICON_SMALL, 0));
    if (!icon) {
        auto ec = std::error_code(::GetLastError(), std::system_category());
        LOG_WARN(log, "cannot get icon via SendMessage: {}", ec);
        icon = reinterpret_cast<HICON>(GetClassLongPtrW(main_handle, GCLP_HICONSM));
    }
    if (!icon) {
        auto ec = std::error_code(::GetLastError(), std::system_category());
        LOG_WARN(log, "cannot get icon via GetClassLongPtr: {}", ec);
        icon = LoadIcon(nullptr, IDI_APPLICATION);
    }
    if (!icon) {
        return;
    }
    this->icon = CopyIcon(icon);

    tray_message = ::RegisterWindowMessageW(tray_message_str);
    if (!tray_message) {
        auto ec = std::error_code(::GetLastError(), std::system_category());
        LOG_WARN(log, "cannot get icon via RegisterWindowMessage: {}", ec);
        return;
    }

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
    if (parent_proc) {
        SetWindowLongPtrW(handle, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(parent_proc));
    }
    if (property) {
        RemovePropW(handle, tray_property_str);
    }
    if (icon) {
        DestroyIcon(icon);
    }
    if (handle) {
        DestroyWindow(handle);
    }
    if (has_window_class) {
        UnregisterClassW(tray_window_class_str, instance);
    }
}

bool tray_win32_t::is_enabled() noexcept { return valid && shown; }

#endif