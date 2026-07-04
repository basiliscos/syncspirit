// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#pragma once

#include "syncspirit-fltk-config.h"

#if defined(SYNCSPIRIT_FLTK_X11)

#include "platform/tray_base.h"
#include <X11/Xatom.h>
#include <X11/Xlib.h>

namespace syncspirit::fltk {

struct app_supervisor_t;
struct tray_x11_t;

struct tray_window_t;

struct tray_x11_t final : tray_impl_t {
    static tray_x11_t *init(app_supervisor_t &) noexcept;

    tray_x11_t(Display *watching_display, Atom selection_atom, Atom opcode_atom, Atom xembed_atom,
               Atom xembed_info_atom, tray_window_t *tray_window, Window owner, Window w, app_supervisor_t &sup);
    tray_x11_t(const tray_x11_t &) = delete;
    tray_x11_t(tray_x11_t &&) = delete;
    ~tray_x11_t();

    bool is_enabled() noexcept override;
    void set_default_icon() noexcept override;
    void set_traffic_icon() noexcept override;

    Display *watching_display = nullptr;
    Atom selection_atom;
    Atom opcode_atom;
    Atom xembed_atom;
    Atom xembed_info_atom;
    Window owner;
    Window window;
    tray_window_t *tray_window;
};

} // namespace syncspirit::fltk

#endif
