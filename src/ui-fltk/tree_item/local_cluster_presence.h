// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include "presence_item.h"

namespace syncspirit::fltk::tree_item {

struct local_cluster_presence_t final : presence_item_t {
    using parent_t = presence_item_t;

    local_cluster_presence_t(presentation::presence_t &presence, app_supervisor_t &supervisor, Fl_Tree *tree);
    bool on_select() override;
};

} // namespace syncspirit::fltk::tree_item
