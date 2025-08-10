// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "model/cluster.h"
#include "pending_devices.h"
#include "some_devices.hpp"
#include "model/diff/cluster_visitor.h"

using namespace syncspirit::model::diff::load;

auto pending_devices_t::apply_impl(cluster_t &cluster, apply_controller_t &controller, void *custom) const noexcept
    -> outcome::result<void> {
    using device_t = model::pending_device_t;
    auto &map = cluster.get_pending_devices();
    auto r = some_devices_t::apply<device_t>(devices, map);
    return r ? applicator_t::apply_sibling(cluster, controller, custom) : r;
}

auto pending_devices_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    return visitor(*this, custom);
}
