// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "update_peer.h"
#include "remove_ignored_device.h"
#include "remove_unknown_device.h"
#include "db/prefix.h"
#include "../cluster_visitor.h"
#include "model/cluster.h"
#include "model/misc/error_code.h"
#include "utils/format.hpp"

using namespace syncspirit::model::diff::modify;

update_peer_t::update_peer_t(db::Device db, const model::device_id_t &device_id,
                             const model::cluster_t &cluster) noexcept
    : item{std::move(db)}, peer_id{device_id.get_sha256()} {
    auto &devices = cluster.get_devices();
    auto peer = devices.by_sha256(peer_id);
    if (!peer) {
        auto &ignored_devices = cluster.get_ignored_devices();
        auto &unknown_devices = cluster.get_unknown_devices();
        auto current = (cluster_diff_t *){nullptr};
        if (auto unknown_device = unknown_devices.by_sha256(peer_id); unknown_device) {
            auto diff = cluster_diff_ptr_t{};
            diff = new remove_unknown_device_t(*unknown_device);
            current = assign_child(diff);
        }
        if (auto ignored_device = ignored_devices.by_sha256(peer_id); ignored_device) {
            auto diff = cluster_diff_ptr_t{};
            diff = new remove_ignored_device_t(*ignored_device);
            current = current ? current->assign_sibling(diff.get()) : assign_child(diff);
        }
    }
}

auto update_peer_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    auto r = applicator_t::apply_child(cluster);
    if (!r) { return r; }
    auto &devices = cluster.get_devices();
    auto peer = devices.by_sha256(peer_id);
    if (!peer) {
        auto prefix = (char)db::prefix::device;
        auto device_id_opt = device_id_t::from_sha256(peer_id);
        if (!device_id_opt) {
            return make_error_code(error_code_t::malformed_deviceid);
        }
        std::string key = std::string(&prefix, 1) + peer_id;
        auto device_opt = device_t::create(key, item);
        if (!device_opt) {
            return device_opt.assume_error();
        }
        peer = device_opt.assume_value();
        devices.put(peer);
    } else {
        peer->update(item);
        peer->notify_update();
    }
    LOG_TRACE(log, "applyging update_peer_t, device {}", peer->device_id());
    return applicator_t::apply_sibling(cluster);
}

auto update_peer_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting update_peer_t");
    return visitor(*this, custom);
}
