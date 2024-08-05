// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "devices.h"
#include "model/cluster.h"
#include "model/misc/error_code.h"
#include "model/diff/cluster_visitor.h"

using namespace syncspirit::model::diff::load;

auto devices_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    auto &device_map = cluster.get_devices();
    auto &local_device = cluster.get_device();
    for (auto &pair : devices) {
        auto device = device_ptr_t();
        if (pair.key == local_device->get_key()) {
            device = local_device;
        } else {
            auto data = pair.value;
            auto db = db::Device();
            auto ok = db.ParseFromArray(data.data(), data.size());
            if (!ok) {
                return make_error_code(error_code_t::device_deserialization_failure);
            }
            auto option = device_t::create(pair.key, db);
            if (!option) {
                return option.assume_error();
            }
            device = std::move(option.value());
        }
        device_map.put(device);
    }
    assert(device_map.by_sha256(local_device->device_id().get_sha256()));
    return next ? next->apply(cluster) : outcome::success();
}

auto devices_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    return visitor(*this, custom);
}
