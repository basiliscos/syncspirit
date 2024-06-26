// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#include "add_ignored_device.h"
#include "model/cluster.h"
#include "model/diff/cluster_visitor.h"

using namespace syncspirit::model::diff::modify;

add_ignored_device_t::add_ignored_device_t(const device_id_t &id_, db::SomeDevice db_device_) noexcept
    : device_id{id_}, db_device{db_device_} {}

auto add_ignored_device_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    auto opt = ignored_device_t::create(device_id, db_device);
    if (!opt) {
        return opt.assume_error();
    }
    auto &ignored_device = opt.assume_value();
    cluster.get_ignored_devices().put(std::move(ignored_device));
    return outcome::success();
}

auto add_ignored_device_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting add_ignored_device_t");
    return visitor(*this, custom);
}
