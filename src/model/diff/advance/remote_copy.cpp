// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "remote_copy.h"
#include "../cluster_visitor.h"
#include "model/cluster.h"
#include "proto/proto-helpers.h"

using namespace syncspirit::model::diff::advance;

remote_copy_t::remote_copy_t(const cluster_t &cluster, sequencer_t &sequencer, proto::FileInfo proto_file_,
                             std::string_view folder_id_, utils::bytes_view_t peer_id_,
                             bool disable_blocks_removal_) noexcept
    : advance_t(folder_id_, peer_id_, advance_action_t::remote_copy, disable_blocks_removal_) {
    initialize(cluster, sequencer, std::move(proto_file_), {});
}

auto remote_copy_t::apply_impl(cluster_t &cluster, apply_controller_t &controller, void *custom) const noexcept
    -> outcome::result<void> {
    auto folder = cluster.get_folders().by_id(folder_id);
    if (!folder) {
        LOG_DEBUG(log, "remote_copy_t, folder = {}, name = {}, folder is not available, ignoring", folder_id,
                  proto::get_name(proto_source));
    } else if (folder->is_suspended()) {
        LOG_DEBUG(log, "remote_copy_t, folder = {}, name = {}, folder is suspended, ignoring", folder_id,
                  proto::get_name(proto_source));
    } else if (auto peer_folder = folder->get_folder_infos().by_device_id(peer_id); !peer_folder) {
        LOG_DEBUG(log, "remote_copy_t, folder = {}, name = {}, peer folder is not available, ignoring", folder_id,
                  proto::get_name(proto_source));
    } else {
        return advance_t::apply_impl(cluster, controller, custom);
    }
    return applicator_t::apply_sibling(cluster, controller, custom);
}

auto remote_copy_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting remote_copy_t, folder = {}, file = {}", folder_id, proto::get_name(proto_local));
    return visitor(*this, custom);
}
