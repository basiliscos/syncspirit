// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#pragma once

#include <FL/Fl_Sys_Menu_Bar.H>
#include <vector>

namespace syncspirit::fltk {

struct app_supervisor_t;

struct menu_t : Fl_Sys_Menu_Bar {
    using parent_t = Fl_Sys_Menu_Bar;
    menu_t(app_supervisor_t &supervisor, int x, int y, int w, int h);

    void on_local_state_update() noexcept;
    void finish_submenu() noexcept;
    void add_item(const char *, int shortcut, Fl_Callback *, void * = 0, int = 0) noexcept;

    using items_t = std::vector<Fl_Menu_Item>;

    app_supervisor_t &supervisor;
    items_t items;
};

} // namespace syncspirit::fltk
