// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include "tree_item.h"
#include "presentation/presence.h"
#include "presentation/entity.h"

namespace syncspirit::fltk {

struct presence_item_t;

using presence_item_ptr_t = model::intrusive_ptr_t<presence_item_t>;

struct presence_item_t : dynamic_item_t, model::augmentation_t {
    using parent_t = dynamic_item_t;
    using parent_t::on_update;

    presence_item_t(presentation::presence_t &presence, app_supervisor_t &supervisor, Fl_Tree *tree,
                    bool augment = true);
    ~presence_item_t();

    bool show(std::uint32_t hide_mask, bool refresh_labels, std::int32_t depth) override;
    bool is_expanded() const;
    void on_open() override;
    presentation::presence_t &get_presence();
    void show_child(presentation::presence_t &child_presence, std::uint32_t mask);
    void update_label() override;
    presence_item_ptr_t safe_detach(int child_index);

  protected:
    void insert_node(presence_item_ptr_t node, int position);
    void populate_dummy_child();
    Fl_Color get_color() const;
    void do_show(std::uint32_t mask, bool refresh_label);
    presence_item_ptr_t do_hide();
    int get_position(const presence_item_t &child, std::uint32_t cut_mask);
    virtual const model::device_t *get_device() const;

    presentation::presence_t *presence;
    bool expanded;
};

} // namespace syncspirit::fltk
