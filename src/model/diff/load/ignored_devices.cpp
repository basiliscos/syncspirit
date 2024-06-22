// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "ignored_devices.h"
#include "model/cluster.h"
#include "model/diff/cluster_visitor.h"

using namespace syncspirit::model::diff::load;

auto ignored_devices_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    auto &map = cluster.get_ignored_devices();

    for (auto &pair : devices) {
        auto key = pair.key;
        auto sha256 = key.substr(1, ignored_device_t::digest_length);
        auto device_id_opt = device_id_t::from_sha256(sha256);
        if (!device_id_opt) {
            return make_error_code(error_code_t::malformed_deviceid);
        }
        auto &device_id = *device_id_opt;

        auto data = pair.value;
        db::SomeDevice db;
        auto ok = db.ParseFromArray(data.data(), data.size());
        if (!ok) {
            return make_error_code(error_code_t::some_device_deserialization_failure);
        }
        auto option = ignored_device_t::create(device_id, db);
        if (!option) {
            return option.assume_error();
        }
        auto &ignored_device = option.assume_value();
        map.put(std::move(ignored_device));
    }
    return outcome::success();
}

auto ignored_devices_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    return visitor(*this, custom);
}
