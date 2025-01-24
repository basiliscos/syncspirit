// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#include "checkbox.h"

static constexpr int padding = 2;

using namespace syncspirit::fltk::table_widget;

Fl_Widget *checkbox_t::create_widget(int x, int y, int w, int h) {
    auto group = new Fl_Group(x, y, w, h);
    group->begin();
    group->box(FL_FLAT_BOX);
    auto yy = y + padding, ww = 25, hh = h - padding * 2;
    input = new Fl_Check_Button(x + padding, yy, ww, hh);
    // auto xx = input->x() + ww + padding;

    group->end();
    widget = group;
    reset();
    return widget;
}
