// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#include "pending_devices.h"
#include "pending_device.h"

using namespace syncspirit;
using namespace syncspirit::model::diff;
using namespace syncspirit::fltk;
using namespace syncspirit::fltk::tree_item;

pending_devices_t::pending_devices_t(app_supervisor_t &supervisor, Fl_Tree *tree) : parent_t(supervisor, tree, false) {
    supervisor.set_pending_devices(this);
    update_label();
}

void pending_devices_t::update_label() {
    auto cluster = supervisor.get_cluster();
    auto count = cluster ? cluster->get_pending_devices().size() : 0;
    auto l = fmt::format("pending devices ({})", count);
    label(l.data());
}

auto pending_devices_t::add_device(model::pending_device_t &device) -> augmentation_ptr_t {
    return within_tree([&]() {
        auto node = insert_by_label(new pending_device_t(device, supervisor, tree()));
        update_label();
        return node->get_proxy();
    });
}

void pending_devices_t::remove_device(tree_item_t *item) {
    remove_child(item);
    update_label();
    tree()->redraw();
}
