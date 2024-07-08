#include "tree_item.h"
#include <algorithm>

using namespace syncspirit::fltk;

tree_item_t::tree_item_t(app_supervisor_t &supervisor_, Fl_Tree *tree)
    : parent_t(tree), supervisor{supervisor_}, content{nullptr} {
    augmentation = new augmentation_t(this);
}

tree_item_t::~tree_item_t() { augmentation->release_onwer(); }

bool tree_item_t::on_select() { return false; }

void tree_item_t::on_desect() { content = nullptr; }

void tree_item_t::on_update() {}

void tree_item_t::on_delete() {}

void tree_item_t::select_other() {
    auto t = tree();
    bool unselect = tree()->first_selected_item() == this;
    if (unselect) {
        Fl_Tree_Item *current = this;
        while (true) {
            auto prev = t->next_item(current, FL_Up, true);
            auto item = dynamic_cast<tree_item_t *>(prev);
            current = prev;
            if (item) {
                if (item->on_select()) {
                    t->select(prev, 0);
                    t->redraw();
                    break;
                }
            }
        }
    }
}

auto tree_item_t::get_proxy() -> augmentation_ptr_t { return augmentation; }

auto tree_item_t::insert_by_label(tree_item_t *child_node, int start_index, int end_index) -> tree_item_t * {
    auto new_label = std::string_view(child_node->label());
    auto end = std::max(end_index, children());
    int pos = start_index;
    for (int i = start_index; i < end_index; ++i) {
        auto label = std::string_view(child(i)->label());
        if (label >= new_label) {
            break;
        }
        ++pos;
    }

    auto tmp_node = insert(prefs(), "", pos);
    replace_child(tmp_node, child_node);
    return child_node;
}
