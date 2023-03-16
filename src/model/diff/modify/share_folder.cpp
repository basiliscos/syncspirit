// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#include "share_folder.h"
#include "../cluster_visitor.h"
#include "../../cluster.h"
#include "../../misc/error_code.h"
#include "structs.pb.h"

using namespace syncspirit::model::diff::modify;

auto share_folder_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    auto &folders = cluster.get_folders();
    auto folder = folders.by_id(folder_id);
    if (!folder) {
        return make_error_code(error_code_t::folder_does_not_exist);
    }

    auto &devices = cluster.get_devices();
    auto &unknown = cluster.get_unknown_folders();

    auto peer = devices.by_sha256(peer_id);
    if (!peer) {
        return make_error_code(error_code_t::device_does_not_exist);
    }
    auto index = uint64_t{0};
    auto max_sequence = int64_t{0};
    auto unknown_it = unknown.end();

    for (auto it = unknown.begin(), prev = unknown.before_begin(); it != unknown.end(); prev = it, ++it) {
        auto &uf = **it;
        if (uf.device_id() == peer->device_id() && uf.get_id() == folder_id) {
            index = uf.get_index();
            max_sequence = uf.get_max_sequence();
            unknown_it = prev;
            break;
        }
    }
    LOG_TRACE(log, "applyging share_folder_t, folder {} with device {}, index = {}, max sequence = {}", folder_id,
              peer->device_id(), index, max_sequence);

    auto folder_info = folder->get_folder_infos().by_device(*peer);
    if (folder_info) {
        return make_error_code(error_code_t::folder_is_already_shared);
    }

    auto db = db::FolderInfo();
    db.set_index_id(index);
    db.set_max_sequence(max_sequence);
    auto fi_opt = folder_info_t::create(cluster.next_uuid(), db, peer, folder);
    if (!fi_opt) {
        return fi_opt.assume_error();
    }

    auto &fi = fi_opt.value();
    folder->add(fi);
    if (index) {
        unknown.erase_after(unknown_it);
    }

    return outcome::success();
}

auto share_folder_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting share_folder_t");
    return visitor(*this, custom);
}
