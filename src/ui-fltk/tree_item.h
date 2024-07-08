#pragma once

#include "app_supervisor.h"
#include "augmentation.h"

#include <FL/Fl_Tree_Item.H>
#include <FL/Fl_Tree.H>

namespace syncspirit::fltk {

namespace outcome = model::outcome;
namespace diff = model::diff;

struct tree_item_t : Fl_Tree_Item {
    using parent_t = Fl_Tree_Item;
    tree_item_t(app_supervisor_t &supervisor, Fl_Tree *tree);
    ~tree_item_t();

    virtual void update_label();
    virtual void refresh_content();
    virtual void remove_child(tree_item_t *child);

    virtual bool on_select();
    virtual void on_desect();
    virtual void on_update();
    virtual void on_delete();

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

    auto insert_by_label(tree_item_t *child, int start_index = 0, int end_index = -1) -> tree_item_t *;

    app_supervisor_t &supervisor;
    Fl_Widget *content;
    augmentation_ptr_t augmentation;
};

} // namespace syncspirit::fltk
