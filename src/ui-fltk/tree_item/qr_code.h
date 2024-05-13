#pragma once

#include "../tree_item.h"
#include "FL/Fl_Widget.H"

namespace syncspirit::fltk::tree_item {

struct qr_code_t : tree_item_t {
    using parent_t = tree_item_t;
    qr_code_t(app_supervisor_t &supervisor, Fl_Tree *tree);

    void on_select() override;
};

} // namespace syncspirit::fltk::tree_item
