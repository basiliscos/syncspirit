// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#pragma once

#include <FL/Fl_Menu_Bar.H>

namespace syncspirit::fltk {

struct app_supervisor_t;

struct menu_t : Fl_Menu_Bar {
    using parent_t = Fl_Menu_Bar;
    menu_t(app_supervisor_t &supervisor, int x, int y, int w, int h);

    app_supervisor_t &supervisor;
};

} // namespace syncspirit::fltk
