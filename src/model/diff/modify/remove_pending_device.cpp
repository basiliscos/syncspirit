// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#include "remove_pending_device.h"
#include "model/cluster.h"
#include "model/diff/cluster_visitor.h"

using namespace syncspirit::model::diff::modify;

remove_pending_device_t::remove_pending_device_t(const pending_device_t &device) noexcept
    : device_key{device.get_key()} {}

auto remove_pending_device_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    auto &pending_devices = cluster.get_pending_devices();
    auto pending_device = pending_devices.by_sha256(get_device_sha256());
    if (!pending_device) {
        return make_error_code(error_code_t::no_such_device);
    }
    pending_devices.remove(pending_device);
    return applicator_t::apply_sibling(cluster);
}

std::string_view remove_pending_device_t::get_device_sha256() const noexcept {
    return std::string_view(device_key).substr(1);
}

auto remove_pending_device_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting remove_pending_device_t");
    return visitor(*this, custom);
}
