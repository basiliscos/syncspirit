// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#pragma once

#include "../tree_item.h"
#include "FL/Fl_Widget.H"

namespace syncspirit::fltk::tree_item {

struct self_device_t final : tree_item_t {
    using parent_t = tree_item_t;
    self_device_t(model::device_t &self, app_supervisor_t &supervisor, Fl_Tree *tree);

    bool on_select() override;
    void update_label() override;
};

} // namespace syncspirit::fltk::tree_item
