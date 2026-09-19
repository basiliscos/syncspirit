// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "window_controller.h"
#include "window_controller/window_controller_win32.h"
#include "main_window.h"
#include "syncspirit-fltk-config.h"

using namespace syncspirit::fltk;

window_controller_impl_t::window_controller_impl_t(main_window_t *main_window_) noexcept : main_window{main_window_} {}

void window_controller_impl_t::show() noexcept {
    using parent_t = typename main_window_t::parent_t;
    main_window->parent_t::show();
}

void window_controller_impl_t::hide() noexcept {
    using parent_t = typename main_window_t::parent_t;
    main_window->parent_t::hide();
}

window_controller_t::window_controller_t(main_window_t *main_window) noexcept {
#if defined(SYNCSPIRIT_FLTK_WIN32)
    impl.reset(new window_controller_impl_win32_t(main_window));
#else
    impl.reset(new window_controller_impl_t(main_window));
#endif
}

void window_controller_t::show() noexcept {
    if (impl) {
        impl->show();
    }
}

void window_controller_t::hide() noexcept {
    if (impl) {
        impl->hide();
    }
}
