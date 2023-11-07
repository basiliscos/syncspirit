// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2023 Ivan Baidakou

#include "unshare_folder.h"
#include "../cluster_visitor.h"
#include "../../cluster.h"
#include "../../misc/error_code.h"
#include "../../../utils/format.hpp"
#include "structs.pb.h"

using namespace syncspirit::model::diff::modify;

auto unshare_folder_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    auto &folders = cluster.get_folders();
    auto folder = folders.by_id(folder_id);
    if (!folder) {
        return make_error_code(error_code_t::folder_does_not_exist);
    }

    auto &devices = cluster.get_devices();

    auto peer = devices.by_sha256(peer_id);
    if (!peer) {
        return make_error_code(error_code_t::device_does_not_exist);
    }

    auto &folder_infos = folder->get_folder_infos();
    auto folder_info = folder_infos.by_device_id(peer_id);
    if (!folder_info) {
        return make_error_code(error_code_t::folder_is_not_shared);
    }

    folder_infos.remove(folder_info);

    LOG_TRACE(log, "applyging unshare_folder_t, folder {} with device {}", folder_id, peer->device_id());

    return outcome::success();
}

auto unshare_folder_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting unshare_folder_t");
    return visitor(*this, custom);
}
