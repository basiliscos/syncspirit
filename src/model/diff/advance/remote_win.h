// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include "advance.h"

namespace syncspirit::model::diff::advance {

struct SYNCSPIRIT_API remote_win_t final : advance_t {
    using parent_t = advance_t;
    using parent_t::parent_t;

    remote_win_t(const cluster_t &cluster, sequencer_t &sequencer, proto::FileInfo proto_file,
                 std::string_view folder_id, std::string_view peer_id) noexcept;

    outcome::result<void> apply_impl(cluster_t &, apply_controller_t &) const noexcept override;

    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;
};

} // namespace syncspirit::model::diff::advance
