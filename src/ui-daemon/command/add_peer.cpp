// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "add_peer.h"
#include "../governor_actor.h"
#include "../error_code.h"
#include "model/diff/modify/update_peer.h"

namespace syncspirit::daemon::command {

outcome::result<command_ptr_t> add_peer_t::construct(std::string_view in) noexcept {
    auto colon = in.find(":");
    if (colon == in.npos) {
        return make_error_code(error_code_t::missing_device_label);
    }

    auto label = in.substr(0, colon);

    auto opt = model::device_id_t::from_string(in.substr(colon + 1));
    if (!opt) {
        return make_error_code(error_code_t::invalid_device_id);
    }
    return std::make_unique<command::add_peer_t>(opt.value(), label);
}

bool add_peer_t::execute(governor_actor_t &actor) noexcept {
    using namespace model::diff;
    log = actor.log;
    auto &cluster = *actor.cluster;
    auto &devices = cluster.get_devices();
    auto found = devices.by_sha256(peer.get_sha256());
    if (found) {
        log->warn("device {} is already added, skipping", peer);
        return false;
    }

    db::Device db_dev;
    db_dev.name(label);

    auto diff = cluster_diff_ptr_t(new modify::update_peer_t(std::move(db_dev), peer, cluster));
    actor.send_command(std::move(diff), *this);
    return true;
}

} // namespace syncspirit::daemon::command
