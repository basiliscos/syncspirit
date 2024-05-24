#pragma once

#pragma once

#include "../tree_item.h"

namespace syncspirit::fltk::tree_item {

struct devices_t : tree_item_t {
    using parent_t = tree_item_t;
    devices_t(app_supervisor_t &supervisor, Fl_Tree *tree);

    void on_select() override;
};

} // namespace syncspirit::fltk::tree_item
