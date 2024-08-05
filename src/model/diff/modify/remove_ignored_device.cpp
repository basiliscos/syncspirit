// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#include "remove_ignored_device.h"
#include "model/cluster.h"
#include "model/diff/cluster_visitor.h"

using namespace syncspirit::model::diff::modify;

remove_ignored_device_t::remove_ignored_device_t(const ignored_device_t &device) noexcept
    : device_key{device.get_key()} {}

auto remove_ignored_device_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    auto &ignored_devices = cluster.get_ignored_devices();
    auto ignored_device = ignored_devices.by_sha256(get_device_sha256());
    if (!ignored_device) {
        return make_error_code(error_code_t::no_such_device);
    }
    ignored_devices.remove(ignored_device);
    return next ? next->apply(cluster) : outcome::success();
}

std::string_view remove_ignored_device_t::get_device_sha256() const noexcept {
    return std::string_view(device_key).substr(1);
}

auto remove_ignored_device_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting remove_ignored_device_t");
    return visitor(*this, custom);
}
