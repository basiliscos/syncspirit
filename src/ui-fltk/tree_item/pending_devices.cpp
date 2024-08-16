#include "pending_devices.h"
#include "pending_device.h"

using namespace syncspirit;
using namespace syncspirit::model::diff;
using namespace syncspirit::fltk;
using namespace syncspirit::fltk::tree_item;

unknown_devices_t::unknown_devices_t(app_supervisor_t &supervisor, Fl_Tree *tree) : parent_t(supervisor, tree, false) {
    supervisor.set_unknown_devices(this);
    update_label();
}

void unknown_devices_t::update_label() {
    auto cluster = supervisor.get_cluster();
    auto count = cluster ? cluster->get_unknown_devices().size() : 0;
    auto l = fmt::format("unknown devices ({})", count);
    label(l.data());
}

auto unknown_devices_t::add_device(model::unknown_device_t &device) -> augmentation_ptr_t {
    return within_tree([&]() {
        auto node = insert_by_label(new unknown_device_t(device, supervisor, tree()));
        update_label();
        return node->get_proxy();
    });
}

void unknown_devices_t::remove_device(tree_item_t *item) {
    remove_child(item);
    update_label();
    tree()->redraw();
}
