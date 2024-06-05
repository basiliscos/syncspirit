#include "tree_item.h"

using namespace syncspirit::fltk;

tree_item_t::tree_item_t(app_supervisor_t &supervisor_, Fl_Tree *tree)
    : parent_t(tree), supervisor{supervisor_}, content{nullptr} {}

void tree_item_t::on_select() {}

void tree_item_t::on_desect() { content = nullptr; }
