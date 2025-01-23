// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#include "../static_table.h"

#include <FL/Fl_Int_Input.H>

namespace syncspirit::fltk::table_widget {

struct int_input_t : widgetable_t {
    using parent_t = widgetable_t;
    using parent_t::parent_t;

    Fl_Widget *create_widget(int x, int y, int w, int h) override;
    Fl_Int_Input *input = nullptr;
};

} // namespace syncspirit::fltk::table_widget
