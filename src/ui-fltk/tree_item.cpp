#include "tree_item.h"

using namespace syncspirit::fltk;

tree_item_t::tree_item_t(app_supervisor_t &supervisor_, Fl_Tree *tree)
    : parent_t(tree), supervisor{supervisor_}, content{nullptr} {}

bool tree_item_t::on_select() { return false; }

void tree_item_t::on_desect() { content = nullptr; }

void tree_item_t::select_other() {
    auto t = tree();
    Fl_Tree_Item *current = this;
    while (true) {
        auto prev = t->next_item(current, FL_Up, true);
        auto item = dynamic_cast<tree_item_t *>(prev);
        current = prev;
        if (item) {
            if (item->on_select()) {
                t->select(prev);
                t->redraw();
                t->remove(this);
                break;
            }
        }
    }
}
