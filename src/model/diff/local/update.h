// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include <set>
#include <string_view>
#include "syncspirit-export.h"
#include "bep.pb.h"
#include "model/diff/cluster_diff.h"
#include "model/misc/sequencer.h"
#include "utils/string_comparator.hpp"

namespace syncspirit::model::diff::local {

struct SYNCSPIRIT_API update_t final : cluster_diff_t {
    using blocks_t = std::set<std::string, utils::string_comparator_t>;

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
