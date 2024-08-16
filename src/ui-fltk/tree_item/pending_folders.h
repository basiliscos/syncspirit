// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#pragma once

#include "../tree_item.h"
#include "model/device.h"

namespace syncspirit::fltk::tree_item {

struct pending_folders_t : tree_item_t {
    using parent_t = tree_item_t;

    pending_folders_t(model::device_t &peer, app_supervisor_t &supervisor, Fl_Tree *tree);

    augmentation_ptr_t add_pending_folder(model::pending_folder_t &uf);
    void remove_folder(tree_item_t *item);

    model::device_t &peer;
};

}; // namespace syncspirit::fltk::tree_item
