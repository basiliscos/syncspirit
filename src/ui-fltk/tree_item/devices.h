#pragma once

#include "model/device.h"
#include "../tree_item.h"
#include "peer_device.h"

namespace syncspirit::fltk::tree_item {

struct devices_t : tree_item_t {
    using parent_t = tree_item_t;
    devices_t(app_supervisor_t &supervisor, Fl_Tree *tree);

    augmentation_ptr_t set_self(model::device_t &self);
    augmentation_ptr_t add_peer(model::device_t &peer);
    peer_device_t *get_peer(const model::device_t &peer);

    bool on_select() override;
    void update_label() override;
    void remove_child(tree_item_t *child) override;
};

} // namespace syncspirit::fltk::tree_item
