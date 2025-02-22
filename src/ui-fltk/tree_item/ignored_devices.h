// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#pragma once

#include "../tree_item.h"

namespace syncspirit::fltk::tree_item {

struct ignored_devices_t : tree_item_t {
    using parent_t = tree_item_t;
    ignored_devices_t(app_supervisor_t &supervisor, Fl_Tree *tree);

    void update_label() override;

    augmentation_ptr_t add_device(model::ignored_device_t &device);
    void remove_device(tree_item_t *item);
};

} // namespace syncspirit::fltk::tree_item
