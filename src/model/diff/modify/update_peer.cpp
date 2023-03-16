// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#include "update_peer.h"
#include "db/prefix.h"
#include "../cluster_visitor.h"
#include "../../cluster.h"
#include "../../misc/error_code.h"

using namespace syncspirit::model::diff::modify;

auto update_peer_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
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
    }
    LOG_TRACE(log, "applyging update_peer_t, device {}", peer->device_id());

    return outcome::success();
}

auto update_peer_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting update_peer_t");
    return visitor(*this, custom);
}
