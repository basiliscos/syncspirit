// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#pragma once

#include "content.h"
#include "tree_item.h"
#include <FL/Fl_Tabs.H>

namespace syncspirit::fltk::content {

struct file_matching_widget_t final : contentable_t<Fl_Group> {
    using parent_t = contentable_t<Fl_Group>;
    file_matching_widget_t(tree_item_t &container, int x, int y, int w, int h);
    void assign(model::folder_ptr_t f);

  private:
    struct table_t;

    tree_item_t &container;
    model::folder_ptr_t folder;
    table_t *table;
};

} // namespace syncspirit::fltk::content
