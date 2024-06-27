// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#include "ignored_connected.h"
#include "model/cluster.h"
#include "../contact_visitor.h"
#include "model/misc/error_code.h"

using namespace syncspirit::model::diff::contact;

ignored_connected_t::ignored_connected_t(cluster_t &cluster, model::ignored_device_t &peer) noexcept : device{peer} {}

auto ignored_connected_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    auto &ignored_devices = cluster.get_ignored_devices();
    auto prev = ignored_devices.by_sha256(device.get_sha256());
    if (prev) {
        return make_error_code(error_code_t::device_already_exists);
    }
    return outcome::success();
}

auto ignored_connected_t::visit(contact_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting ignored_connected_t");
    return visitor(*this, custom);
}
