#pragma once

#include "app_supervisor.h"
#include <FL/Fl_Tree.H>

namespace syncspirit::fltk {

struct tree_view_t : Fl_Tree {
    using parent_t = Fl_Tree;
    tree_view_t(app_supervisor_t &supervisor, int x, int y, int w, int h);

    app_supervisor_t &supervisor;
};

} // namespace syncspirit::fltk
