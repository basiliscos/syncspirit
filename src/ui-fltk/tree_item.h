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
        auto t = tree();
        t->begin();
        auto &&r = fn();
        t->end();
        t->redraw();
        return std::move(r);
    }

    int bisect_pos(std::string_view name, int start_index = 0, int end_index = MAX_INDEX);
    auto insert_by_label(tree_item_t *child, int start_index = 0, int end_index = MAX_INDEX) -> tree_item_t *;

    app_supervisor_t &supervisor;
    content_t *content;
    augmentation_ptr_t augmentation;
};

} // namespace syncspirit::fltk
