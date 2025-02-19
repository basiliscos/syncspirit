// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include "utils/bytes.h"
#include "../cluster_diff.h"

namespace syncspirit::model::diff::load {

struct pair_t {
    utils::bytes_view_t key;
    utils::bytes_view_t value;
};

using container_t = std::vector<pair_t>;

} // namespace syncspirit::model::diff::load
