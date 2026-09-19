// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "syncspirit-fltk-config.h"

#if defined(SYNCSPIRIT_FLTK_WIN32)

#include "main_window.h"
#include "window_controller_win32.h"
#include <FL/Fl.H>
#include <FL/platform.H>

using namespace syncspirit::fltk;

void window_controller_impl_win32_t::hide() noexcept {
    auto &tray = main_window->get_tray();
    auto sup = main_window->get_supervisor();
    if (tray.is_enabled() && sup->get_app_config().fltk_config.hide_to_tray) {
        // seems fltk/win32 destroys the window, do manually hide it:
        HWND hwnd = (HWND)fl_xid(main_window);
        auto is_visible = IsWindowVisible(hwnd) != FALSE;
        if (is_visible) {
            ShowWindow(hwnd, SW_HIDE);
            native_hidden = true;
            return;
        }
    }
    parent_t::hide();
}

void window_controller_impl_win32_t::show() noexcept {
    if (native_hidden) {
        HWND hwnd = (HWND)fl_xid(main_window);
        auto is_visible = IsWindowVisible(hwnd) != FALSE;
        if (!is_visible) {
            native_hidden = false;
            ShowWindow(hwnd, SW_SHOW);
            SetForegroundWindow(hwnd);
            SetActiveWindow(hwnd);
            main_window->redraw();
            main_window->flush();
            return;
        }
    }
    parent_t::show();
}

#endif
