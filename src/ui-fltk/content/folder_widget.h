// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#pragma once

#include "content.h"
#include "tree_item.h"

namespace syncspirit::fltk::content {

struct folder_widget_t : contentable_t<Fl_Group> {
    using parent_t = contentable_t<Fl_Group>;
    enum behavior_t { edit_new, candiate, remote, local };

    struct serialization_context_t {
        db::Folder folder;
        std::uint64_t index;
        model::devices_map_t shared_with;
    };

    folder_widget_t(tree_item_t &container, behavior_t behavior, int x, int y, int w, int h);

    void refresh() override;

    void make_tabs(model::folder_ptr_t f, model::folder_info_ptr_t fi);
    void reset_data();

    inline bool is_new() const noexcept { return behavior == behavior_t::edit_new; }
    inline bool is_local() const noexcept { return behavior == behavior_t::local; }
    inline bool is_remote() const noexcept { return behavior == behavior_t::remote; }
    inline bool is_candidate() const noexcept { return behavior == behavior_t::candiate; }

    void on_apply() noexcept;
    void on_create() noexcept;
    void on_share() noexcept;
    void on_rescan() noexcept;
    void on_reset() noexcept;
    void on_remove() noexcept;

    void create_or_update() noexcept;
    void sync_shares_with_model() noexcept;
    void set_error(std::string_view error) noexcept;

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
    Fl_Widget *tabs_container{nullptr};
};

} // namespace syncspirit::fltk::content
