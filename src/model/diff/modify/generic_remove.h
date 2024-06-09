// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#pragma once

#include "model/diff/cluster_diff.h"
#include "utils/string_comparator.hpp"
#include <set>

namespace syncspirit::model::diff::modify {

struct SYNCSPIRIT_API generic_remove_t : cluster_diff_t {
    using keys_t = std::set<std::string, utils::string_comparator_t>;

    generic_remove_t(keys_t keys) noexcept;

    keys_t keys;
};

} // namespace syncspirit::model::diff::modify
