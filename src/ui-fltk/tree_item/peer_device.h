// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#pragma once

#include "model/device.h"
#include "../tree_item.h"
#include "../table_widget/base.h"
#include <boost/asio.hpp>
#include <vector>

namespace syncspirit::fltk::tree_item {

struct peer_device_t : tree_item_t {
    using parent_t = tree_item_t;
    using widgetable_ptr_t = table_widget::table_widget_ptr_t;

    using widgets_t = std::vector<widgetable_ptr_t>;
    peer_device_t(model::device_t &peer, app_supervisor_t &supervisor, Fl_Tree *tree);

    const model::device_t &get_device() const;
    widgetable_ptr_t record(widgetable_ptr_t);
    std::string get_state();
    void update_label() override;
    void refresh_content() override;

    bool on_select() override;

    void on_remove();
    void on_apply();
    void on_reset();

    tree_item_t *get_unknown_folders();

    model::device_t &peer;
    widgets_t widgets;

    Fl_Widget *apply_button;
    Fl_Widget *reset_button;
};

} // namespace syncspirit::fltk::tree_item
