#pragma once

#include "../tree_item.h"
#include "FL/Fl_Widget.H"

namespace syncspirit::fltk::tree_item {

struct self_device_t : tree_item_t, private model_listener_t {
    using parent_t = tree_item_t;
    self_device_t(app_supervisor_t &supervisor, Fl_Tree *tree);

    void operator()(model::message::model_response_t &) override;
    void on_select() override;
    void update_label();

    Fl_Widget *table;
    model_subscription_t model_sub;
};

} // namespace syncspirit::fltk::tree_item
