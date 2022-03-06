// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#include "ignored_devices.h"
#include "../../cluster.h"

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
