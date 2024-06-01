#pragma once

#pragma once

#include "../tree_item.h"

namespace syncspirit::fltk::tree_item {

struct devices_t : tree_item_t, private model_load_listener_t {
    using parent_t = tree_item_t;
    devices_t(app_supervisor_t &supervisor, Fl_Tree *tree);

    void on_select() override;
    void operator()(model::message::model_response_t &) override;
    void build_tree();

    void add_device(const model::device_ptr_t &device);
    model_subscription_t model_sub;
};

} // namespace syncspirit::fltk::tree_item
