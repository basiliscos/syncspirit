#include "tree_view.h"

using namespace syncspirit::fltk;

static void tree_view_callback(Fl_Widget *w, void *data) {
    Fl_Tree      *tree = (Fl_Tree*)w;
    Fl_Tree_Item *item = (Fl_Tree_Item*)tree->callback_item();    // get selected item
/*
    switch ( tree->callback_reason() ) {
      case FL_TREE_REASON_SELECTED: [..]
      case FL_TREE_REASON_DESELECTED: [..]
      case FL_TREE_REASON_RESELECTED: [..]
      case FL_TREE_REASON_OPENED: [..]
      case FL_TREE_REASON_CLOSED: [..]
    }
*/
}

tree_view_t::tree_view_t(application_t &application_, int x, int y, int w, int h):
    parent_t(x, y, w, h), application{application_}{

    showroot(false);
    auto folders_node = new Fl_Tree_Item(this);
    auto devices_node = new Fl_Tree_Item(this);
    auto self_node = new Fl_Tree_Item(this);

    folders_node->label("folders-label");
    devices_node->label("devices");
    self_node->label("self");

    add("folders", folders_node);
    add("devices", devices_node);
    add("devices/self", self_node);
}
