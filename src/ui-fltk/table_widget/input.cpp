// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#include "input.h"

static constexpr int padding = 2;

using namespace syncspirit::fltk::table_widget;

Fl_Widget *input_t::create_widget(int x, int y, int w, int h) {
    auto group = new Fl_Group(x, y, w, h);
    group->begin();
    group->box(FL_FLAT_BOX);
    auto yy = y + padding, ww = w - padding * 2, hh = h - padding * 2;

    input = new Fl_Input(x + padding, yy, ww, hh);
    group->end();
    widget = group;
    reset();
    return widget;
}
