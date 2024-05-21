#pragma once

#include "../tree_item.h"
#include "category.h"
#include <FL/Fl_Group.H>

namespace syncspirit::fltk::config {

struct control_t : Fl_Group {
    control_t(tree_item_t &tree_item, int x, int y, int w, int h);

    void on_reset();

    tree_item_t &tree_item;
    categories_t categories;
};

} // namespace syncspirit::fltk::config
