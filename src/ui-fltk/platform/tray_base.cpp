// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "tray_base.h"
#include "app_supervisor.h"
#include "syncspirit-fltk-config.h"

#if defined(SYNCSPIRIT_FLTK_X11)
#include "tray_impl/tray_x11.h"
#elif defined(SYNCSPIRIT_FLTK_WIN32)
#include "tray_impl/tray_win32.h"
#endif

using namespace syncspirit::fltk;

static void cb_quit(Fl_Widget *, void *data) {
    auto tray_widget = reinterpret_cast<tray_impl_t *>(data);
    auto &sup = tray_widget->sup;
    sup.get_logger()->info("exiting via menu");
    sup.do_shutdown();
}

tray_impl_t::tray_impl_t(app_supervisor_t &sup_) noexcept : sup{sup_} {
    menu_items.push_back({"Quit", 0, cb_quit, this, 0, 0, 0, 14, 0});
    menu_items.push_back({nullptr});
}

tray_base_t::~tray_base_t() {
    if (impl) {
        delete impl;
    }
}

void tray_base_t::init(app_supervisor_t &sup_) noexcept { sup = &sup_; }

void tray_base_t::enable(bool value) noexcept {
    if (value) {
        if (impl) {
            delete impl;
            impl = nullptr;
        }
#if defined(SYNCSPIRIT_FLTK_X11)
        impl = tray_x11_t::init(*sup);
#elif defined(SYNCSPIRIT_FLTK_WIN32)
        impl = tray_win32_t::init(*sup);
#endif
    } else {
        delete impl;
        impl = nullptr;
    }
}

bool tray_base_t::is_enabled() noexcept {
    if (impl) {
        return impl->is_enabled();
    }
    return false;
}

bool tray_base_t::is_available() noexcept {
#if defined(SYNCSPIRIT_FLTK_X11)
    return true;
#elif defined(SYNCSPIRIT_FLTK_WIN32)
    return true;
#endif
    return false;
}
