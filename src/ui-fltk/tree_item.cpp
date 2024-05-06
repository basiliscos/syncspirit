#include "tree_item.h"

using namespace syncspirit::fltk;

tree_item_t::tree_item_t(application_t &application_, Fl_Tree* tree): parent_t(tree), application{application_}{
}

void tree_item_t::on_select() {
    printf("on select\n");
}
