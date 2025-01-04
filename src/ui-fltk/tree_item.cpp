#include "tree_item.h"
#include "utils.hpp"

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
    tree()->redraw();
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

auto tree_item_t::insert_by_label(tree_item_t *child_node, int start_index, int end_index) -> tree_item_t * {
    auto new_label = std::string_view(child_node->label());
    auto name_provider = [this](int index) { return std::string_view(child(index)->label()); };
    auto pos = bisect(new_label, start_index, end_index, children(), name_provider);
    auto tmp_node = insert(prefs(), "", pos);
    replace_child(tmp_node, child_node);
    return child_node;
}

void tree_item_t::remove_child(tree_item_t *child) {
    parent_t::remove_child(child);
    update_label();
    tree()->redraw();
}

void tree_item_t::apply(const node_visitor_t &visitor, void *data) {
    visitor.visit(*this, data);
    auto children_count = children();
    for (int i = 0; i < children_count; ++i) {
        auto node = static_cast<tree_item_t *>(child(i));
        node->apply(visitor, data);
    }
}
