#include "unknown_devices.h"

#include "unknown_device.h"
using namespace syncspirit;
using namespace syncspirit::model::diff;
using namespace syncspirit::fltk;
using namespace syncspirit::fltk::tree_item;

unknown_devices_t::unknown_devices_t(app_supervisor_t &supervisor, Fl_Tree *tree) : parent_t(supervisor, tree) {
    supervisor.set_unknown_devices(this);
    update_label();
}

void unknown_devices_t::update_label() {
    auto cluster = supervisor.get_cluster();
    auto count = cluster ? cluster->get_unknown_devices().size() : 0;
    auto l = fmt::format("unknown devices ({})", count);
    label(l.data());
    tree()->redraw();
}

auto unknown_devices_t::add_device(model::unknown_device_t &device) -> augmentation_ptr_t {
    return within_tree([&]() {
        auto node = new unknown_device_t(device, supervisor, tree());
        add(prefs(), node->label(), node);
        update_label();
        return node->get_proxy();
    });
}

void unknown_devices_t::remove_device(tree_item_t *item) {
    update_label();
    remove_child(item);
    tree()->redraw();
}
