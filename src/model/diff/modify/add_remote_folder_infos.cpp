// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#include "add_remote_folder_infos.h"
#include "model/cluster.h"
#include "model/diff/cluster_visitor.h"
#include "model/misc/error_code.h"

using namespace syncspirit::model::diff::modify;

add_remote_folder_infos_t::add_remote_folder_infos_t(const model::device_t &peer, container_t items) noexcept
    : device_id{peer.device_id().get_sha256()}, container{std::move(items)} {
    LOG_DEBUG(log, "add_remote_folder_infos_t, device = {}, items = {}", peer.device_id().get_short(),
              container.size());
}

auto add_remote_folder_infos_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    auto &devices = cluster.get_devices();
    auto &folders = cluster.get_folders();
    auto peer = devices.by_sha256(device_id);
    if (!peer) {
        return make_error_code(error_code_t::no_such_device);
    }
    auto &remote_folders = peer->get_remote_folder_infos();
    for (auto &item : container) {
        auto folder = folders.by_id(item.folder_id);
        if (!folder) {
            return make_error_code(error_code_t::no_such_folder);
        }
        auto opt = model::remote_folder_info_t::create(item.index_id, item.max_sequence, *peer, *folder);
        if (!opt) {
            return opt.assume_error();
        }
        remote_folders.put(opt.assume_value());
    }
    return applicator_t::apply_sibling(cluster);
}

auto add_remote_folder_infos_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept
    -> outcome::result<void> {
    LOG_TRACE(log, "visiting add_remote_folder_infos_t");
    return visitor(*this, custom);
}
