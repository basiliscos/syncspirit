#include "ignored_devices.h"
#include "ignored_device.h"

using namespace syncspirit;
using namespace syncspirit::model::diff;
using namespace syncspirit::fltk;
using namespace syncspirit::fltk::tree_item;

ignored_devices_t::ignored_devices_t(app_supervisor_t &supervisor, Fl_Tree *tree) : parent_t(supervisor, tree) {
    supervisor.set_ignored_devices(this);
    update_label();
    tree->close(this, 0);
}

void ignored_devices_t::update_label() {
    auto cluster = supervisor.get_cluster();
    auto count = cluster ? cluster->get_ignored_devices().size() : 0;
    auto l = fmt::format("ignored devices ({})", count);
    label(l.data());
    tree()->redraw();
}

augmentation_ptr_t ignored_devices_t::add_device(model::ignored_device_t &device) {
    return within_tree([&]() {
        auto node = insert_by_label(new ignored_device_t(device, supervisor, tree()));
        update_label();
        return node->get_proxy();
    });
}

void ignored_devices_t::remove_device(tree_item_t *item) {
    remove_child(item);
    update_label();
    tree()->redraw();
}

#if 0
auto ignored_devices_t::operator()(const diff::modify::add_ignored_device_t &diff, void *) noexcept
    -> outcome::result<void> {
    auto &ignored_devices = supervisor.get_cluster()->get_ignored_devices();
    auto peer = ignored_devices.by_sha256(diff.device_id.get_sha256());
    add_device(peer);
    return outcome::success();
}
#endif
