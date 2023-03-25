// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2023 Ivan Baidakou

#pragma once

#include <set>
#include <string_view>
#include "syncspirit-export.h"
#include "bep.pb.h"
#include "model/diff/cluster_diff.h"
#include "utils/string_comparator.hpp"

namespace syncspirit::model::diff::modify {

struct SYNCSPIRIT_API new_file_t final : cluster_diff_t {
    using blocks_t = std::set<std::string, utils::string_comparator_t>;

    new_file_t(const cluster_t &cluster, std::string_view folder_id, proto::FileInfo file) noexcept;

    outcome::result<void> apply_impl(cluster_t &) const noexcept override;
    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;

    std::string folder_id;
    proto::FileInfo file;
    blocks_t new_blocks;
    blocks_t removed_blocks;
    bool already_exists;
};

} // namespace syncspirit::model::diff::modify
