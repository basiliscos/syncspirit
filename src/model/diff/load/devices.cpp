// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "devices.h"
#include "model/cluster.h"
#include "model/misc/error_code.h"
#include "model/diff/cluster_visitor.h"
#include "proto/proto-helpers-db.h"

using namespace syncspirit::model::diff::load;

auto devices_t::apply_impl(cluster_t &cluster, apply_controller_t &controller) const noexcept -> outcome::result<void> {
    auto &device_map = cluster.get_devices();
    auto &local_device = cluster.get_device();
    for (auto &pair : devices) {
        auto device = device_ptr_t();
        if (pair.key == local_device->get_key()) {
            device = local_device;
        } else {
            auto db_device = db::Device();
            if (auto ok = db::decode(pair.value, db_device); !ok) {
                return make_error_code(error_code_t::device_deserialization_failure);
            }
            auto option = device_t::create(pair.key, db_device);
            if (!option) {
                return option.assume_error();
            }
            device = std::move(option.value());
        }
        device_map.put(device);
    }
    assert(device_map.by_sha256(local_device->device_id().get_sha256()));
    return applicator_t::apply_sibling(cluster, controller);
}

auto devices_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    return visitor(*this, custom);
}
