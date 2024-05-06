#pragma once

#include "application.h"
#include <FL/Fl_Tree_Item.H>
#include <FL/Fl_Tree.H>

namespace syncspirit::fltk {

struct tree_item_t: Fl_Tree_Item {
    using parent_t = Fl_Tree_Item;
    tree_item_t(application_t &application, Fl_Tree* tree);
    virtual void on_select();

    application_t &application;
};

}
