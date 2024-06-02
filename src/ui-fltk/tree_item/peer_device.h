#pragma once

#include "model/device.h"
#include "../tree_item.h"

namespace syncspirit::fltk::tree_item {

struct peer_device_t : tree_item_t {
    using parent_t = tree_item_t;
    peer_device_t(model::device_ptr_t peer, app_supervisor_t &supervisor, Fl_Tree *tree);

    const model::device_t &get_device() const;
    void on_select() override;

    model::device_ptr_t peer;
    Fl_Widget *table;
};

} // namespace syncspirit::fltk::tree_item
