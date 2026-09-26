// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "syncspirit-fltk-config.h"

#if defined(SYNCSPIRIT_FLTK_MACOS)

#include "app_supervisor.h"
#include "main_window.h"
#include "tray_macos_native.h"
#include "tray_macos.h"
#include <FL/Fl.H>

using namespace syncspirit::fltk;

tray_macos_t *tray_macos_t::init(app_supervisor_t &sup) noexcept {
    auto main_image = sup.get_main_window()->get_icon();
    auto native = syncspirit_mac_tray_create(main_image);
    if (native) {
        return new tray_macos_t(sup, native);
    }

    return nullptr;
}

tray_macos_t::tray_macos_t(app_supervisor_t &sup, void *native_impl_) : tray_impl_t(sup), native_impl{native_impl_} {
    syncspirit_mac_tray_assing_menu(native_impl_, menu_items.data());
    syncspirit_mac_tray_assing_traffic_icon(native_impl_, traffic_image);
    syncspirit_mac_tray_assing_offline_icon(native_impl_, offline_image);
}

tray_macos_t::~tray_macos_t() {
    if (native_impl) {
        syncspirit_mac_tray_destroy(native_impl);
    }
}

bool tray_macos_t::is_enabled() noexcept {
    if (native_impl) {
        return syncspirit_mac_tray_enabled(native_impl);
    }
    return false;
}

void tray_macos_t::set_default_icon() noexcept {
    if (native_impl) {
        return syncspirit_mac_tray_set_default_icon(native_impl);
    }
}

void tray_macos_t::set_traffic_icon() noexcept {
    if (native_impl) {
        return syncspirit_mac_tray_set_traffic_icon(native_impl);
    }
}

void tray_macos_t::set_offline_icon() noexcept {
    if (native_impl) {
        return syncspirit_mac_tray_set_offline_icon(native_impl);
    }
}

#endif
