#pragma once

#include "../tree_item.h"

namespace syncspirit::fltk::tree_item {

struct unknown_devices_t : tree_item_t {
    using parent_t = tree_item_t;
    unknown_devices_t(app_supervisor_t &supervisor, Fl_Tree *tree);

    void update_label();

    augmentation_ptr_t add_device(model::unknown_device_t &device);
    void remove_device(tree_item_t *item);
};

} // namespace syncspirit::fltk::tree_item
