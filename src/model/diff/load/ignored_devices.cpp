// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "ignored_devices.h"
#include "model/cluster.h"
#include "model/diff/cluster_visitor.h"

using namespace syncspirit::model::diff::load;

auto ignored_devices_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    auto &map = cluster.get_ignored_devices();
    for (auto &pair : devices) {
        auto option = ignored_device_t::create(pair.key, pair.value);
        if (!option) {
            return option.assume_error();
        }
        auto &device = option.value();
        map.put(device);
    }
    return outcome::success();
}

auto ignored_devices_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    return visitor(*this, custom);
}
