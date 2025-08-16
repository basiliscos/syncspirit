// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "update_peer.h"
#include "remove_ignored_device.h"
#include "remove_pending_device.h"
#include "db/prefix.h"
#include "model/cluster.h"
#include "model/misc/error_code.h"
#include "model/diff/cluster_visitor.h"
#include "model/diff/apply_controller.h"
#include "utils/format.hpp"

using namespace syncspirit::model::diff::modify;

update_peer_t::update_peer_t(db::Device db, const model::device_id_t &device_id,
                             const model::cluster_t &cluster) noexcept
    : item{std::move(db)} {
    peer_id = device_id.get_sha256();
    LOG_DEBUG(log, "update_peer_t, peer = {}", device_id.get_short());
    auto &ignored_devices = cluster.get_ignored_devices();
    auto &pending_devices = cluster.get_pending_devices();
    auto current = (cluster_diff_t *){nullptr};
    if (auto pending_device = pending_devices.by_sha256(peer_id); pending_device) {
        auto diff = cluster_diff_ptr_t{};
        diff = new remove_pending_device_t(*pending_device);
        current = assign_child(diff);
    }
    if (auto ignored_device = ignored_devices.by_sha256(peer_id); ignored_device) {
        auto diff = cluster_diff_ptr_t{};
        diff = new remove_ignored_device_t(*ignored_device);
        current = current ? current->assign_sibling(diff.get()) : assign_child(diff);
    }
}

auto update_peer_t::apply_forward(cluster_t &cluster, apply_controller_t &controller, void *custom) const noexcept
    -> outcome::result<void> {
    return controller.apply(*this, cluster, custom);
}

auto update_peer_t::apply_impl(cluster_t &cluster, apply_controller_t &controller, void *custom) const noexcept
    -> outcome::result<void> {
    auto r = applicator_t::apply_child(cluster, controller, custom);
    if (!r) {
        return r;
    }
    auto &devices = cluster.get_devices();
    auto peer = devices.by_sha256(peer_id);
    if (!peer) {
        auto prefix = (char)db::prefix::device;
        auto device_id_opt = device_id_t::from_sha256(peer_id);
        if (!device_id_opt) {
            return make_error_code(error_code_t::malformed_deviceid);
        }
        auto key = utils::bytes_t(peer_id.size() + 1);
        key[0] = prefix;
        std::copy(peer_id.begin(), peer_id.end(), key.data() + 1);
        auto device_opt = device_t::create(key, item);
        if (!device_opt) {
            return device_opt.assume_error();
        }
        peer = device_opt.assume_value();
        devices.put(peer);
    } else {
        auto ec = peer->update(item);
        if (!ec) {
            auto &error = ec.error();
            LOG_ERROR(log, "applying update_peer_t, device {} fail: {}", peer->device_id(), error.message());
            return error;
        }
        peer->notify_update();
    }
    LOG_TRACE(log, "applying update_peer_t, device {}", peer->device_id());
    return applicator_t::apply_sibling(cluster, controller, custom);
}

auto update_peer_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting update_peer_t");
    return visitor(*this, custom);
}
