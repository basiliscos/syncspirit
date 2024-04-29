#pragma once

#include <FL/Fl_Group.H>

#include "application.h"

namespace syncspirit::fltk {

struct log_panel_t: Fl_Group {
    using parent_t = Fl_Group;
    log_panel_t(application_t &application, int x, int y, int w, int h);
};

}
