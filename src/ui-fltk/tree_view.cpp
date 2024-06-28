#include "tree_view.h"
#include "tree_item/devices.h"
#include "tree_item/unknown_devices.h"

#include <FL/Fl.H>

using namespace syncspirit::fltk;

static void tree_view_callback(Fl_Widget *w, void *data) {
    auto tree = reinterpret_cast<tree_view_t *>(w);
    auto item = static_cast<tree_item_t *>(tree->callback_item());
    switch (tree->callback_reason()) {
    case FL_TREE_REASON_SELECTED:
        item->on_select();
        break;
    case FL_TREE_REASON_DESELECTED:
        item->on_desect();
        break;
        /*
      case FL_TREE_REASON_RESELECTED: [..]
      case FL_TREE_REASON_OPENED: [..]
      case FL_TREE_REASON_CLOSED: [..]
*/
    }
}

tree_view_t::tree_view_t(app_supervisor_t &supervisor_, int x, int y, int w, int h)
    : parent_t(x, y, w, h), supervisor{supervisor_} {

    showroot(false);
    auto folders_node = new tree_item_t(supervisor, this);
    auto devices_node = new tree_item::devices_t(supervisor, this);
    auto unknown_devices_node = new tree_item::unknown_devices_t(supervisor, this);

    folders_node->label("folders-label");

    add("folders", folders_node);
    add("devices", devices_node);
    add("devices", unknown_devices_node);

    callback(tree_view_callback);
}
