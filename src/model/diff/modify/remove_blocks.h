// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#pragma once

#include "model/diff/cluster_diff.h"
#include "utils/string_comparator.hpp"
#include <set>

namespace syncspirit::model::diff::modify {

struct SYNCSPIRIT_API remove_blocks_t final : cluster_diff_t {
    using keys_t = std::set<std::string, utils::string_comparator_t>;

    remove_blocks_t(keys_t blocks) noexcept;
    outcome::result<void> apply_impl(cluster_t &) const noexcept override;
    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;

    keys_t removed_blocks;
};

} // namespace syncspirit::model::diff::modify
