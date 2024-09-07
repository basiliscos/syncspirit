#include "tree_item.h"

#include "static_table.h"
#include <algorithm>

using namespace syncspirit::fltk;

tree_item_t::tree_item_t(app_supervisor_t &supervisor_, Fl_Tree *tree, bool has_augmentation)
    : parent_t(tree), supervisor{supervisor_}, content{nullptr} {
    if (has_augmentation) {
        augmentation = new augmentation_t(this);
    }
}

tree_item_t::~tree_item_t() {
    if (augmentation) {
        augmentation->release_onwer();
    }
}

bool tree_item_t::on_select() { return false; }

void tree_item_t::on_deselect() { content = nullptr; }

void tree_item_t::on_open() {}

void tree_item_t::on_close() {}

void tree_item_t::update_label() {}

void tree_item_t::refresh_content() {
    if (content) {
        content->refresh();
    }
}

void tree_item_t::on_update() {
    update_label();
    refresh_content();
}

void tree_item_t::on_delete() {
    select_other();
    if (augmentation) {
        augmentation->release_onwer();
    }
    auto upper = static_cast<tree_item_t *>(parent());
    if (upper) {
        upper->remove_child(this);
    }
}

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

int tree_item_t::bisect_pos(std::string_view new_label, int start_index, int end_index) {
    auto children_count = children();
    end_index = std::min(children_count - 1, end_index);
    if (end_index < 0) {
        return 0;
    }

    auto right = std::string_view(child(end_index)->label());
    if (new_label > right) {
        return end_index + 1;
    }
    auto left = std::string_view(child(start_index)->label());
    if (left >= new_label) {
        return start_index;
    }

    // int pos = start_index;
    while (start_index <= end_index) {
        auto left = std::string_view(child(start_index)->label());
        if (left >= new_label) {
            return start_index;
        }
        auto right = std::string_view(child(end_index)->label());
        if (right < new_label) {
            return end_index;
        }

        auto mid_index = (start_index + end_index) / 2;
        auto mid = std::string_view(child(mid_index)->label());
        if (mid < new_label) {
            end_index = mid_index;
        } else {
            start_index = mid_index;
        }
    }
    return start_index;
}

auto tree_item_t::insert_by_label(tree_item_t *child_node, int start_index, int end_index) -> tree_item_t * {
    auto new_label = std::string_view(child_node->label());
    auto pos = bisect_pos(new_label, start_index, end_index);
    auto tmp_node = insert(prefs(), "", pos);
    replace_child(tmp_node, child_node);
    return child_node;
}

void tree_item_t::remove_child(tree_item_t *child) {
    parent_t::remove_child(child);
    update_label();
    tree()->redraw();
}
