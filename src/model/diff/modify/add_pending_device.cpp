// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#include "add_pending_device.h"
#include "model/cluster.h"
#include "model/diff/cluster_visitor.h"

using namespace syncspirit::model::diff::modify;

add_unknown_device_t::add_unknown_device_t(const device_id_t &id_, db::SomeDevice db_device_) noexcept
    : device_id{id_}, db_device{db_device_} {}

auto add_unknown_device_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    auto opt = unknown_device_t::create(device_id, db_device);
    if (!opt) {
        return opt.assume_error();
    }
    auto &unknown_device = opt.assume_value();
    cluster.get_unknown_devices().put(std::move(unknown_device));
    return applicator_t::apply_sibling(cluster);
}

auto add_unknown_device_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting add_unknown_device_t");
    return visitor(*this, custom);
}
