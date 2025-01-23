// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#pragma once

#include "app_supervisor.h"
#include <FL/Fl_Group.H>

namespace syncspirit::fltk {

struct toolbar_t : Fl_Group {
    using parent_t = Fl_Group;
    toolbar_t(app_supervisor_t &supervisor, int x, int y, int w, int h);

    app_supervisor_t &supervisor;
};

} // namespace syncspirit::fltk
