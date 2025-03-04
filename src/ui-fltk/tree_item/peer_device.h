// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#pragma once

#include "model/device.h"
#include "../tree_item.h"

namespace syncspirit::fltk::tree_item {

struct peer_device_t : tree_item_t {
    using parent_t = tree_item_t;

    peer_device_t(model::device_t &peer, app_supervisor_t &supervisor, Fl_Tree *tree);

    const model::device_t &get_device() const;
    std::string_view get_state();

    void update_label() override;
    bool on_select() override;
    void remove_child(tree_item_t *child) override;

    tree_item_t *get_folders();
    tree_item_t *get_pending_folders();

    model::device_t &peer;
    tree_item_t *folders;
    tree_item_t *pending_folders;
};

} // namespace syncspirit::fltk::tree_item
