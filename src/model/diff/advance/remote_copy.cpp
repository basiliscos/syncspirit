// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "remote_copy.h"
#include "../cluster_visitor.h"

using namespace syncspirit::model::diff::advance;

remote_copy_t::remote_copy_t(const cluster_t &cluster, sequencer_t &sequencer, proto::FileInfo proto_file_,
                             std::string_view folder_id_, std::string_view peer_id_) noexcept
    : advance_t(std::move(proto_file_), folder_id_, peer_id_, advance_action_t::remote_copy) {
    initialize(cluster, sequencer, proto_source.name());
}

auto remote_copy_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting remote_copy_t, folder = {}, file = {}", folder_id, proto_local.name());
    return visitor(*this, custom);
}
