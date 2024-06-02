#pragma once

#include "app_supervisor.h"

#include <FL/Fl_Group.H>

namespace syncspirit::fltk {

struct qr_button_t : Fl_Group {
    using parent_t = Fl_Group;

    qr_button_t(model::device_ptr_t device, app_supervisor_t &supervisor, int x, int y, int w, int h);

    model::device_ptr_t device;
    app_supervisor_t &supervisor;
};

} // namespace syncspirit::fltk
