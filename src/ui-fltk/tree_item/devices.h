#pragma once

#include "model/device.h"
#include "../tree_item.h"

namespace syncspirit::fltk::tree_item {

struct devices_t : tree_item_t {
    using parent_t = tree_item_t;
    devices_t(app_supervisor_t &supervisor, Fl_Tree *tree);

    augmentation_ptr_t set_self(model::device_t &self);
    augmentation_ptr_t add_peer(model::device_t &peer);
    void remove_peer(tree_item_t *item);

    bool on_select() override;
    void update_label() override;
    void add_new_device(std::string_view device_id, std::string_view label);

    size_t devices_count;
};

} // namespace syncspirit::fltk::tree_item
