// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#pragma once

#include "model/device.h"
#include "../tree_item.h"
#include "../static_table.h"
#include <boost/asio.hpp>
#include <vector>
#include <optional>

namespace syncspirit::fltk::tree_item {

struct unknown_folders_t : tree_item_t, private model_listener_t, private model::diff::cluster_visitor_t {
    using parent_t = tree_item_t;

    unknown_folders_t(model::device_ptr_t peer, app_supervisor_t &supervisor, Fl_Tree *tree);

    model_subscription_t model_sub;
    model::device_ptr_t peer;
};

}; // namespace syncspirit::fltk::tree_item
