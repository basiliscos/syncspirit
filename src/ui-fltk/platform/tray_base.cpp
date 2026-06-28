// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "tray_base.h"
#include "app_supervisor.h"
#include "syncspirit-fltk-config.h"

#if defined(SYNCSPIRIT_FLTK_X11)
#include "tray_impl/tray_x11.h"
#endif

using namespace syncspirit::fltk;

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
#endif
    return false;
}
