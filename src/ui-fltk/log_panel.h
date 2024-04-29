#pragma once

#include "application.h"

#include <FL/Fl_Group.H>
#include <FL/Fl_Toggle_Button.H>
#include <array>

namespace syncspirit::fltk {

struct log_panel_t : Fl_Group {
    using parent_t = Fl_Group;
    using level_buttons_t = std::array<Fl_Toggle_Button *, 6>;

    log_panel_t(application_t &application, int x, int y, int w, int h);
    Fl_Widget *log_table;
    level_buttons_t level_buttons;
};

} // namespace syncspirit::fltk
