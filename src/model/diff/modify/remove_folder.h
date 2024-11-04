// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#pragma once

#include "model/folder.h"
#include "model/diff/cluster_diff.h"
#include "model/misc/sequencer.h"

namespace syncspirit::model::diff::modify {

struct SYNCSPIRIT_API remove_folder_t final : cluster_diff_t {
    using parent_t = cluster_diff_t;

    remove_folder_t(const model::cluster_t &cluster, model::sequencer_t &sequencer,
                    const model::folder_t &folder) noexcept;

    outcome::result<void> apply_impl(cluster_t &) const noexcept override;
    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;

    std::string folder_id;
    std::string folder_key;
};

} // namespace syncspirit::model::diff::modify
