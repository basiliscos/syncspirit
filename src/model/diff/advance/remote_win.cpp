// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "remote_win.h"
#include "remote_copy.h"
#include "model/cluster.h"
#include "../cluster_visitor.h"
#include "proto/proto-helpers.h"

using namespace syncspirit::model::diff::advance;

remote_win_t::remote_win_t(const cluster_t &cluster, sequencer_t &sequencer, proto::FileInfo proto_file_,
                           std::string_view folder_id_, utils::bytes_view_t peer_id_) noexcept
    : advance_t(folder_id_, peer_id_, advance_action_t::resolve_remote_win) {
    auto &self = *cluster.get_device();
    auto folder = cluster.get_folders().by_id(folder_id);
    auto &local_files = folder->get_folder_infos().by_device(self)->get_file_infos();
    auto local_file = local_files.by_name(proto::get_name(proto_file_));
    auto local_source = local_file->as_proto(true);
    initialize(cluster, sequencer, std::move(local_source), local_file->make_conflicting_name());
    assign_sibling(new remote_copy_t(cluster, sequencer, std::move(proto_file_), folder_id_, peer_id_, true));
}

auto remote_win_t::apply_impl(cluster_t &cluster, apply_controller_t &controller) const noexcept
    -> outcome::result<void> {
    auto &self = *cluster.get_device();
    auto folder = cluster.get_folders().by_id(folder_id);
    if (!folder) {
        LOG_DEBUG(log, "remote_win_t, folder = {}, name = {}, folder is not available, ignoring", folder_id,
                  proto::get_name(proto_source));
    } else if (folder->is_suspended()) {
        LOG_DEBUG(log, "remote_win_t, folder = {}, name = {}, folder is suspended, ignoring", folder_id,
                  proto::get_name(proto_source));
    } else if (auto peer_folder = folder->get_folder_infos().by_device_id(peer_id); !peer_folder) {
        LOG_DEBUG(log, "remote_win_t, folder = {}, name = {}, peer folder is not available, ignoring", folder_id,
                  proto::get_name(proto_source));
    } else {
        auto &local_files = folder->get_folder_infos().by_device(self)->get_file_infos();
        auto prev_file = local_files.by_name(proto::get_name(proto_source));
        assert(prev_file);
        local_files.remove(prev_file);
        return parent_t::apply_impl(cluster, controller);
    }
    return applicator_t::apply_sibling(cluster, controller);
}

auto remote_win_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting remote_win_t, folder = {}, file = {}", folder_id, proto::get_name(proto_source));
    return visitor(*this, custom);
}
