// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include <string_view>
#include "syncspirit-export.h"
#include "bep.pb.h"
#include "model/diff/cluster_diff.h"
#include "model/misc/sequencer.h"

namespace syncspirit::model::diff::local {

struct SYNCSPIRIT_API update_t final : cluster_diff_t {
    update_t(const cluster_t &cluster, sequencer_t &sequencer, std::string_view folder_id,
             proto::FileInfo file) noexcept;

    outcome::result<void> apply_impl(cluster_t &) const noexcept override;
    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;

    std::string folder_id;
    bu::uuid uuid;
    proto::FileInfo file;
    bool already_exists;
};

} // namespace syncspirit::model::diff::local
