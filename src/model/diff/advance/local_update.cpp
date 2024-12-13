// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "local_update.h"
#include "model/cluster.h"
#include "model/misc/version_utils.h"
#include "../cluster_visitor.h"

using namespace syncspirit::model::diff::advance;

local_update_t::local_update_t(const cluster_t &cluster, sequencer_t &sequencer, proto::FileInfo proto_file_,
                               std::string_view folder_id_) noexcept
    : advance_t(std::move(proto_file_), folder_id_, cluster.get_device()->device_id().get_sha256(),
                advance_action_t::local_update) {
    auto &device = *cluster.get_devices().by_sha256(peer_id);
    auto folder = cluster.get_folders().by_id(folder_id);
    auto folder_id = folder->get_id();
    auto &self = *cluster.get_device();
    auto self_id = self.device_id().get_sha256();

    auto &local_folder_infos = folder->get_folder_infos();
    auto local_folder_info = local_folder_infos.by_device_id(self_id);
    auto &local_files = local_folder_info->get_file_infos();
    auto local_file = local_files.by_name(proto_file.name());
    auto &proto_version = *proto_file.mutable_version();

    auto version = version_ptr_t();
    if (local_file) {
        version = local_file->get_version();
        version->update(device);
    } else {
        version.reset(new version_t(device));
    }
    version->to_proto(proto_version);
    initialize(cluster, sequencer);
}

auto local_update_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting local_update_t, folder = {}, file = {}", folder_id, proto_file.name());
    return visitor(*this, custom);
}
