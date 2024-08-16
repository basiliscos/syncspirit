// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#include "unknown_connected.h"
#include "model/cluster.h"
#include "../contact_visitor.h"
#include "model/misc/error_code.h"
#include "model/diff/modify/add_pending_device.h"

using namespace syncspirit::model::diff::contact;

unknown_connected_t::unknown_connected_t(cluster_t &cluster, const model::device_id_t &device_id_,
                                         db::SomeDevice db_device_) noexcept
    : device_id{device_id_}, db_device{std::move(db_device_)} {}

auto unknown_connected_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    auto &unknown_devices = cluster.get_unknown_devices();
    auto prev = unknown_devices.by_sha256(device_id.get_sha256());
    if (!prev) {
        return make_error_code(error_code_t::no_such_device);
    }
    prev->assign(db_device);
    prev->notify_update();
    return applicator_t::apply_sibling(cluster);
}

auto unknown_connected_t::visit(contact_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting unknown_connected_t");
    return visitor(*this, custom);
}
