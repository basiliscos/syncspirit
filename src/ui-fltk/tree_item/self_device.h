#pragma once

#include "../tree_item.h"

namespace syncspirit::fltk::tree_item {

struct self_device_t: tree_item_t, private model_load_listener_t {
    using parent_t = tree_item_t;
    self_device_t(app_supervisor_t &supervisor, Fl_Tree* tree);
    ~self_device_t();

    void operator()(model::message::model_response_t&) override;
    void on_select() override;
};

}
