// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#pragma once

#include "model/pending_device.h"
#include "../tree_item.h"

namespace syncspirit::fltk::tree_item {

struct pending_device_t : tree_item_t {
    using parent_t = tree_item_t;

    pending_device_t(model::pending_device_t &device, app_supervisor_t &supervisor, Fl_Tree *tree);
    void update_label() override;

    bool on_select() override;

    model::pending_device_t &device;
};

}; // namespace syncspirit::fltk::tree_item
