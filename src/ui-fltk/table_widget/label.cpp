// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#include "label.h"

static constexpr int padding = 2;

using namespace syncspirit::fltk::table_widget;

Fl_Widget *label_t::create_widget(int x, int y, int w, int h) {
    input = new Fl_Box(x, y, w, h, "");
    input->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    widget = input;
    reset();
    return widget;
}
