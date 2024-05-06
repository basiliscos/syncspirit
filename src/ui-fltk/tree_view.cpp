#include "tree_view.h"

#include "tree_item.h"


using namespace syncspirit::fltk;

static void tree_view_callback(Fl_Widget *w, void *data) {
    auto tree = reinterpret_cast<tree_view_t*>(w);
    auto item = static_cast<tree_item_t*>(tree->callback_item());
    switch ( tree->callback_reason() ) {
      case FL_TREE_REASON_SELECTED: item->on_select(); break;
        /*
      case FL_TREE_REASON_DESELECTED: [..]
      case FL_TREE_REASON_RESELECTED: [..]
      case FL_TREE_REASON_OPENED: [..]
      case FL_TREE_REASON_CLOSED: [..]
*/
    }
}

tree_view_t::tree_view_t(application_t &application_, int x, int y, int w, int h):
    parent_t(x, y, w, h), application{application_}{

    showroot(false);
    auto folders_node = new tree_item_t(application, this);
    auto devices_node = new tree_item_t(application, this);
    auto self_node = new tree_item_t(application, this);

    folders_node->label("folders-label");
    devices_node->label("devices");
    self_node->label("self");

    add("folders", folders_node);
    add("devices", devices_node);
    add("devices/self", self_node);

    callback(tree_view_callback);
}
