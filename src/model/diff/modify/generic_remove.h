// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#pragma once

#include "model/diff/cluster_diff.h"
#include "utils/bytes_comparator.hpp"
#include "utils/bytes.h"
#include <set>

namespace syncspirit::model::diff::modify {

struct SYNCSPIRIT_API generic_remove_t : cluster_diff_t {
    using unique_keys_t = std::set<utils::bytes_t, utils::bytes_comparator_t>;
    using keys_t = std::vector<utils::bytes_t>;

    generic_remove_t() = default;
    generic_remove_t(keys_t keys) noexcept;
    generic_remove_t(unique_keys_t keys) noexcept;

    keys_t keys;
};

} // namespace syncspirit::model::diff::modify
