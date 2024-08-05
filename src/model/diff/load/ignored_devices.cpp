// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "model/cluster.h"
#include "ignored_devices.h"
#include "some_devices.hpp"
#include "model/diff/cluster_visitor.h"

using namespace syncspirit::model::diff::load;

auto ignored_devices_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    using device_t = model::ignored_device_t;
    auto &map = cluster.get_ignored_devices();
    auto r = some_devices_t::apply<device_t>(devices, map);
    return r && next ? next->apply(cluster) : r;
}

auto ignored_devices_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    return visitor(*this, custom);
}
