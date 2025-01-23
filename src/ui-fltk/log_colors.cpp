// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#include "log_colors.h"

namespace syncspirit::fltk {

color_array_t log_colors = {
    fl_rgb_color(220, 255, 220), fl_rgb_color(240, 255, 240), // trace
    fl_rgb_color(210, 245, 255), fl_rgb_color(230, 250, 255), // debug
    fl_rgb_color(245, 245, 245), fl_rgb_color(255, 255, 255), // info
    fl_rgb_color(255, 250, 200), fl_rgb_color(255, 250, 220), // warn
    fl_rgb_color(255, 220, 220), fl_rgb_color(255, 240, 240), // error
    fl_rgb_color(255, 220, 255), fl_rgb_color(255, 240, 255)  // critical
};

Fl_Color table_selection_color = FL_GREEN;

} // namespace syncspirit::fltk
