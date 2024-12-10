// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include "advance.h"

namespace syncspirit::model::diff::advance {

struct SYNCSPIRIT_API local_update_t final : advance_t {
    using parent_t = advance_t;
    using parent_t::parent_t;

    local_update_t(const cluster_t &cluster, sequencer_t &sequencer, proto::FileInfo proto_file,
                   std::string_view folder_id) noexcept;

    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;
};

} // namespace syncspirit::model::diff::advance
