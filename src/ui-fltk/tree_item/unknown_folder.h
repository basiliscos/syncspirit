// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#pragma once

#include "model/unknown_folder.h"
#include "../tree_item.h"
#include "../static_table.h"
#include <boost/asio.hpp>
#include <vector>
#include <optional>

namespace syncspirit::fltk::tree_item {

struct unknown_folder_t : tree_item_t, private model_listener_t, private model::diff::cluster_visitor_t {
    using parent_t = tree_item_t;

    unknown_folder_t(model::unknown_folder_ptr_t folder, app_supervisor_t &supervisor, Fl_Tree *tree);

    bool on_select() override;

    model::unknown_folder_ptr_t folder;
};

}; // namespace syncspirit::fltk::tree_item
