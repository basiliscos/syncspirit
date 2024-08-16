// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#pragma once

#include "model/pending_folder.h"
#include "../tree_item.h"

namespace syncspirit::fltk::tree_item {

struct unknown_folder_t : tree_item_t {
    using parent_t = tree_item_t;

    unknown_folder_t(model::unknown_folder_t &folder, app_supervisor_t &supervisor, Fl_Tree *tree);

    bool on_select() override;

    model::unknown_folder_t &folder;
};

}; // namespace syncspirit::fltk::tree_item
