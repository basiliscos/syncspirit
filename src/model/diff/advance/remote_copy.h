// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include "advance.h"

namespace syncspirit::model::diff::advance {

struct SYNCSPIRIT_API remote_copy_t final : advance_t {
    using parent_t = advance_t;
    using parent_t::parent_t;

    remote_copy_t(const cluster_t &cluster, sequencer_t &sequencer, proto::FileInfo proto_file,
                  std::string_view folder_id, utils::bytes_view_t peer_id,
                  bool disable_blocks_removal = false) noexcept;

    outcome::result<void> apply_impl(cluster_t &, apply_controller_t &, void *) const noexcept override;

    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;
};

} // namespace syncspirit::model::diff::advance
