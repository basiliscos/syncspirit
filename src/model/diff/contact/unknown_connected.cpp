// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#include "unknown_connected.h"
#include "model/cluster.h"
#include "../contact_visitor.h"
#include "model/misc/error_code.h"

using namespace syncspirit::model::diff::contact;

unknown_connected_t::unknown_connected_t(cluster_t &cluster, model::unknown_device_t &peer) noexcept : device{peer} {
    auto &unknown_devices = cluster.get_unknown_devices();
    is_new = !unknown_devices.by_sha256(device.get_sha256());
}

auto unknown_connected_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    auto &unknown_devices = cluster.get_unknown_devices();
    auto prev = unknown_devices.by_sha256(device.get_sha256());
    if (is_new) {
        if (prev) {
            return make_error_code(error_code_t::device_already_exists);
        }
        unknown_devices.put(unknown_device_ptr_t(new unknown_device_t(device)));
    } else {
        if (!prev) {
            return make_error_code(error_code_t::no_such_device);
        }
        prev->set_last_seen(device.get_last_seen());
    }
    return outcome::success();
}

auto unknown_connected_t::visit(contact_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting unknown_connected_t");
    return visitor(*this, custom);
}
