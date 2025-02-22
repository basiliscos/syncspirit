// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#include "ignored_devices.h"
#include "ignored_device.h"

using namespace syncspirit;
using namespace syncspirit::model::diff;
using namespace syncspirit::fltk;
using namespace syncspirit::fltk::tree_item;

ignored_devices_t::ignored_devices_t(app_supervisor_t &supervisor, Fl_Tree *tree) : parent_t(supervisor, tree, false) {
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
