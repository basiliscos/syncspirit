// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#pragma once

#include "content.h"
#include "tree_item.h"
#include <FL/Fl_Tabs.H>

namespace syncspirit::fltk::content {


struct folder_widget_t : contentable_t<Fl_Tabs> {

    enum behavior_t {
        edit_new,
        candiate,
        remote,
        local
    };

    using parent_t = contentable_t<Fl_Tabs>;

    folder_widget_t(tree_item_t &container, behavior_t behavior, int x, int y, int w, int h);
    void make_tabs(const model::folder_info_t &description);

    Fl_Widget& make_details_tab(int x, int y, int w, int h);
    Fl_Widget& make_sharing_tab(int x, int y, int w, int h);
    Fl_Widget& make_file_patterns_tab(int x, int y, int w, int h);

    tree_item_t &container;
    const model::folder_info_t *description{nullptr};
    behavior_t behavior;
    std::string error;
};


}
