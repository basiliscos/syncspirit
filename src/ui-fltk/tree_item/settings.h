#pragma once

#include "../tree_item.h"

namespace syncspirit::fltk::tree_item {

struct settings_t : tree_item_t {
    using parent_t = tree_item_t;
    settings_t(app_supervisor_t &supervisor, Fl_Tree *tree);

    void update_label(bool modified);
    void on_select() override;
};

} // namespace syncspirit::fltk::tree_item
