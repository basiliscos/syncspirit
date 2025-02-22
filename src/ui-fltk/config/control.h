// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#pragma once

#include "../tree_item.h"
#include "category.h"
#include <FL/Fl_Group.H>

namespace syncspirit::fltk::config {

struct control_t : contentable_t<Fl_Group> {
    using parent_t = contentable_t<Fl_Group>;

    control_t(tree_item_t &tree_item, int x, int y, int w, int h);

    void on_reset();
    void on_save();
    void on_setting_modify();

    tree_item_t &tree_item;
    categories_t categories;
    main_cfg_t default_cfg;
};

} // namespace syncspirit::fltk::config
