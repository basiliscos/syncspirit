// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#pragma once

#include "model/device.h"
#include "../table_widget/base.h"

namespace syncspirit::fltk::tree_item {

struct peer_device_t : tree_item_t {
    using parent_t = tree_item_t;

    peer_device_t(model::device_t &peer, app_supervisor_t &supervisor, Fl_Tree *tree);

    const model::device_t &get_device() const;
    std::string get_state();

    void update_label() override;
    void refresh_content() override;
    bool on_select() override;

    void on_remove();
    void on_apply();
    void on_reset();

    tree_item_t *get_unknown_folders();

    model::device_t &peer;

    Fl_Widget *apply_button;
    Fl_Widget *reset_button;
};

} // namespace syncspirit::fltk::tree_item
