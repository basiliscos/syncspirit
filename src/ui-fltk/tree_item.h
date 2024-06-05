#pragma once

#include "app_supervisor.h"
#include <FL/Fl_Tree_Item.H>
#include <FL/Fl_Tree.H>

namespace syncspirit::fltk {

namespace outcome = model::outcome;
namespace diff = model::diff;

struct tree_item_t : Fl_Tree_Item {
    using parent_t = Fl_Tree_Item;
    tree_item_t(app_supervisor_t &supervisor, Fl_Tree *tree);
    virtual void on_select();
    virtual void on_desect();

    app_supervisor_t &supervisor;
    Fl_Widget *content;
};

} // namespace syncspirit::fltk
