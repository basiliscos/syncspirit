// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include "../tree_item.h"
#include "presentation/presence.h"
#include "presentation/entity.h"

namespace syncspirit::fltk::tree_item {

struct presence_item_t : dynamic_item_t, model::augmentation_t {
    using parent_t = dynamic_item_t;

    presence_item_t(presentation::presence_t &presence, app_supervisor_t &supervisor, Fl_Tree *tree);
    ~presence_item_t();

    void show(std::uint32_t hide_mask, bool refresh_labels, bool recurse) override;
    void refresh_children() override;
    void on_open() override;
    presentation::presence_t &get_presence();

  protected:
    Fl_Color get_color() const;
    void do_show(std::uint32_t mask, bool refresh_label);
    void do_hide();
    int get_position(std::uint32_t cut_mask);
    void populate_dummy_child();

    presentation::presence_t &presence;
    bool expanded;
};

} // namespace syncspirit::fltk::tree_item
