#include "devices.h"
#include "self_device.h"

using namespace syncspirit::fltk::tree_item;

devices_t::devices_t(app_supervisor_t &supervisor, Fl_Tree *tree) : parent_t(supervisor, tree) {
    label("devices");

    auto self_node = new tree_item::self_device_t(supervisor, tree);

    add(prefs(), "self", self_node);
}

void devices_t::on_select() {}
