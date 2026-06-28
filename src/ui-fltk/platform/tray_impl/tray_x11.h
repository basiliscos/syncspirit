// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#pragma once

#include "syncspirit-fltk-config.h"

#if defined(SYNCSPIRIT_FLTK_X11)

#include "platform/tray_base.h"
#include <FL/Fl_Menu_Item.H>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <vector>

namespace syncspirit::fltk {

struct app_supervisor_t;
struct tray_x11_t;

struct tray_window_t;

struct tray_x11_t final : tray_impl_t {
    using menu_items_t = std::vector<Fl_Menu_Item>;
    static tray_x11_t *init(app_supervisor_t &) noexcept;

    tray_x11_t(Atom selection_atom, Atom opcode_atom, Atom xembed_atom, Atom xembed_info_atom,
               tray_window_t *tray_window, Window owner, Window w, app_supervisor_t &sup);
    tray_x11_t(const tray_x11_t &) = delete;
    tray_x11_t(tray_x11_t &&) = delete;
    ~tray_x11_t();

    bool is_enabled() noexcept override;

    Atom selection_atom;
    Atom opcode_atom;
    Atom xembed_atom;
    Atom xembed_info_atom;
    Window owner;
    Window window;
    tray_window_t *tray_window;
    menu_items_t menu_items;
    app_supervisor_t &sup;
};

} // namespace syncspirit::fltk

#endif
