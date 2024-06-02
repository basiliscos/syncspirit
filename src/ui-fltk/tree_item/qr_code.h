#pragma once

#include "../tree_item.h"

namespace syncspirit::fltk::tree_item {

struct qr_code_t : tree_item_t {
    using parent_t = tree_item_t;
    qr_code_t(app_supervisor_t &supervisor, Fl_Tree *tree);

    void on_select() override;
    void set_device(model::device_ptr_t device);

    model::device_ptr_t device;
};

} // namespace syncspirit::fltk::tree_item
