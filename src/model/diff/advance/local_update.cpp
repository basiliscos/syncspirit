// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "local_update.h"
#include "model/cluster.h"
#include "../cluster_visitor.h"

using namespace syncspirit::model::diff::advance;

local_update_t::local_update_t(const cluster_t &cluster, sequencer_t &sequencer, proto::FileInfo proto_file_,
                               std::string_view folder_id_) noexcept
    : advance_t(std::move(proto_file_), folder_id_, cluster.get_device()->device_id().get_sha256(),
                advance_action_t::local_update) {
    initialize(cluster, sequencer);
}

auto local_update_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting local_update_t, folder = {}, file = {}", folder_id, proto_file.name());
    return visitor(*this, custom);
}
