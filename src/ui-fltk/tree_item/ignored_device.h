// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#pragma once

#include "model/ignored_device.h"
#include "../tree_item.h"
#include "../static_table.h"

namespace syncspirit::fltk::tree_item {

struct ignored_device_t : tree_item_t {
    using parent_t = tree_item_t;

    ignored_device_t(model::ignored_device_ptr_t device, app_supervisor_t &supervisor, Fl_Tree *tree);
    void update_label();
    void refresh();

    bool on_select() override;
    void on_connect();
    void on_remove();

    model::ignored_device_ptr_t device;
};

}; // namespace syncspirit::fltk::tree_item
