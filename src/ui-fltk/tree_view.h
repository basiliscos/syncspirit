#pragma once

#include "application.h"
#include <FL/Fl_Tree.H>

namespace syncspirit::fltk {

struct tree_view_t: Fl_Tree {
    using parent_t = Fl_Tree;
    tree_view_t(application_t &application, int x, int y, int w, int h);

    application_t &application;
};

}
