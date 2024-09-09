#include "tree_view.h"
#include "tree_item/devices.h"
#include "tree_item/folders.h"
#include "tree_item/ignored_devices.h"
#include "tree_item/pending_devices.h"

#include <FL/Fl.H>

using namespace syncspirit::fltk;

static void tree_view_callback(Fl_Widget *w, void *data) {
    auto tree = reinterpret_cast<tree_view_t *>(w);
    auto item = static_cast<tree_item_t *>(tree->callback_item());
    switch (tree->callback_reason()) {
    case FL_TREE_REASON_SELECTED:
        if (tree->current != item) {
            tree->current = item;
            item->on_select();
        }
        break;
    case FL_TREE_REASON_DESELECTED:
        item->on_deselect();
        tree->current = nullptr;
        break;
        /*
      case FL_TREE_REASON_RESELECTED: [..]
        */
    case FL_TREE_REASON_OPENED:
        item->on_open();
        break;
    case FL_TREE_REASON_CLOSED:
        item->on_close();
        break;
    }
}

tree_view_t::tree_view_t(app_supervisor_t &supervisor_, int x, int y, int w, int h)
    : parent_t(x, y, w, h), supervisor{supervisor_}, current{nullptr} {
    resizable(this);
    showroot(false);
    auto folders_node = new tree_item::folders_t(supervisor, this);
    auto devices_node = new tree_item::devices_t(supervisor, this);
    auto pending_devices_node = new tree_item::pending_devices_t(supervisor, this);
    auto ignored_devices_node = new tree_item::ignored_devices_t(supervisor, this);

    add(folders_node->label(), folders_node);
    add(devices_node->label(), devices_node);
    add(pending_devices_node->label(), pending_devices_node);
    add(ignored_devices_node->label(), ignored_devices_node);

    callback(tree_view_callback);
}
