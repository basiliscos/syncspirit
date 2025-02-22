// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#pragma once

#include "app_supervisor.h"
#include "augmentation.h"
#include "content.h"

#include <FL/Fl_Tree_Item.H>
#include <FL/Fl_Tree.H>
#include <limits>

namespace syncspirit::fltk {

namespace outcome = model::outcome;
namespace diff = model::diff;

struct static_table_t;
struct node_visitor_t;

struct tree_item_t : Fl_Tree_Item {
    using parent_t = Fl_Tree_Item;
    static constexpr auto MAX_INDEX = std::numeric_limits<int>::max();

    tree_item_t(app_supervisor_t &supervisor, Fl_Tree *tree, bool has_augmentation = true);
    ~tree_item_t();

    virtual void update_label();
    void refresh_content();
    virtual void remove_child(tree_item_t *child);

    virtual bool on_select();
    virtual void on_deselect();
    virtual void on_update();
    virtual void on_delete();
    virtual void on_open();
    virtual void on_close();

    void select_other();
    augmentation_ptr_t get_proxy();

    template <typename Fn> auto within_tree(Fn &&fn) {
        using result_t = decltype(fn());
        if constexpr (std::is_same_v<result_t, void>) {
            auto t = tree();
            t->begin();
            fn();
            t->end();
            t->redraw();
            return;
        } else {
            auto t = tree();
            t->begin();
            auto &&r = fn();
            t->end();
            t->redraw();
            return std::move(r);
        }
    }

    void apply(const node_visitor_t &visitor, void *data);
    auto insert_by_label(tree_item_t *child, int start_index = 0, int end_index = MAX_INDEX) -> tree_item_t *;

    app_supervisor_t &supervisor;
    content_t *content;
    augmentation_ptr_t augmentation;
};

struct dynamic_item_t : tree_item_t {
    using parent_t = tree_item_t;
    using parent_t::parent_t;
    virtual dynamic_item_t *create(augmentation_entry_t &) = 0;
    virtual void show_deleted(bool value) = 0;
    virtual void refresh_children() = 0;
};

struct node_visitor_t {
    virtual ~node_visitor_t() = default;
    virtual void visit(tree_item_t &node, void *) const = 0;
};

} // namespace syncspirit::fltk
