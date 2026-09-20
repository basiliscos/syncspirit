// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "syncspirit-fltk-config.h"

#if defined(SYNCSPIRIT_FLTK_MACOS)

#include "main_window.h"
#include "window_controller_macos.h"
#include "app_helper_native.h"
#include <Fl/Fl.H>

using namespace syncspirit::fltk;

static void quit_callback(void *data) {
    auto main_window = reinterpret_cast<main_window_t *>(data);
    auto sup = main_window->get_supervisor();
    auto &log = sup->get_logger();
    LOG_DEBUG(log, "terminating via app quit");
    sup->do_shutdown();
}

static void post_init_install_quit_interceptor(void *data) {
    auto main_window = reinterpret_cast<main_window_t *>(data);
    auto &log = main_window->get_supervisor()->get_logger();
    if (!syncspirit_mac_app_install_quit_handler(quit_callback, data)) {
        LOG_WARN(log, "fail to install app quit handler");
    } else {
        LOG_DEBUG(log, "successfully installed app quit handler");
    }
}

window_controller_impl_macos_t::window_controller_impl_macos_t(main_window_t *main_window_) noexcept
    : parent_t(main_window_) {
    Fl::add_timeout(0.0, post_init_install_quit_interceptor, main_window);
}

void window_controller_impl_macos_t::hide() noexcept {
    auto &tray = main_window->get_tray();
    auto sup = main_window->get_supervisor();
    if (tray.is_enabled() && sup->get_app_config().fltk_config.hide_to_tray) {
        native_hidden = true;
        syncspirit_mac_app_helper_hide();
        return;
    }
    parent_t::hide();
}

void window_controller_impl_macos_t::show() noexcept {
    if (native_hidden) {
        syncspirit_mac_app_helper_show();
        native_hidden = false;
        return;
    }
    parent_t::show();
}

#endif
