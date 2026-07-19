// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#pragma once

#include "content.h"
#include "tree_item.h"
#include <FL/Fl_Tabs.H>

namespace syncspirit::fltk::content {

struct folder_widget_t : contentable_t<Fl_Tabs> {
    using parent_t = contentable_t<Fl_Tabs>;
    enum behavior_t { edit_new, candiate, remote, local };

    struct serialization_context_t {
        db::Folder folder;
        std::uint64_t index;
        model::devices_map_t shared_with;
    };

    folder_widget_t(tree_item_t &container, behavior_t behavior, int x, int y, int w, int h);
    void make_tabs(model::folder_ptr_t f, model::folder_info_ptr_t fi);
    void reset_data();

    Fl_Widget &make_details_tab(int x, int y, int w, int h);
    Fl_Widget &make_sharing_tab(int x, int y, int w, int h);
    Fl_Widget &make_file_patterns_tab(int x, int y, int w, int h);

    void on_apply() noexcept;
    void on_create() noexcept;
    void on_share() noexcept;
    void on_rescan() noexcept;
    void on_remove() noexcept;

    void create_or_update() noexcept;
    void sync_shares_with_model() noexcept;

    tree_item_t &container;
    model::folder_ptr_t folder;
    model::folder_ptr_t folder_orig;
    model::folder_info_ptr_t folder_info;
    model::folder_info_ptr_t folder_info_orig;
    model::devices_map_t shared_with;
    model::devices_map_t shared_with_orig;
    model::devices_map_t non_shared_with;
    model::devices_map_t non_shared_with_orig;
    behavior_t behavior;
    std::string error;
};

} // namespace syncspirit::fltk::content
