// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "local_update.h"
#include "model/cluster.h"
#include "../cluster_visitor.h"
#include "proto/proto-helpers.h"

using namespace syncspirit::model::diff::advance;

local_update_t::local_update_t(const cluster_t &cluster, sequencer_t &sequencer, proto::FileInfo proto_file_,
                               std::string_view folder_id_) noexcept
    : advance_t(folder_id_, cluster.get_device()->device_id().get_sha256(), advance_action_t::local_update) {

    auto &device = *cluster.get_devices().by_sha256(peer_id);
    auto folder = cluster.get_folders().by_id(folder_id);
    auto &self = *cluster.get_device();
    auto self_id = self.device_id().get_sha256();

    auto &local_folder_infos = folder->get_folder_infos();
    auto local_folder_info = local_folder_infos.by_device_id(self_id);
    auto &local_files = local_folder_info->get_file_infos();
    auto name = std::string(proto::get_name(proto_file_));
    auto local_file = local_files.by_name(name);
    proto::set_modified_by(proto_file_, self.device_id().get_uint());

    initialize(cluster, sequencer, std::move(proto_file_), name);

    auto version = version_ptr_t();
    if (local_file) {
        version = local_file->get_version();
        version->update(device);
    } else {
        version.reset(new version_t(device));
    }
    auto &proto_version = proto::get_version(proto_local);
    version->to_proto(proto_version);
}

auto local_update_t::apply_impl(cluster_t &cluster, apply_controller_t &controller) const noexcept
    -> outcome::result<void> {
    auto folder = cluster.get_folders().by_id(folder_id);
    if (!folder) {
        LOG_DEBUG(log, "remote_copy_t, folder = {}, name = {}, folder is not available, ignoring", folder_id,
                  proto::get_name(proto_source));
    } else if (folder->is_suspended()) {
        LOG_DEBUG(log, "remote_copy_t, folder = {}, name = {}, folder is suspended, ignoring", folder_id,
                  proto::get_name(proto_source));
    } else {
        return advance_t::apply_impl(cluster, controller);
    }
    return applicator_t::apply_sibling(cluster, controller);
}

auto local_update_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting local_update_t, folder = {}, file = {}", folder_id, proto::get_name(proto_local));
    return visitor(*this, custom);
}
