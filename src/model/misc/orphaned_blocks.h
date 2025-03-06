// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#pragma once

#include "model/file_info.h"
#include "utils/bytes_comparator.hpp"
#include "utils/bytes.h"
#include <set>

namespace syncspirit::model {

struct SYNCSPIRIT_API orphaned_blocks_t {
    using set_t = std::set<utils::bytes_t, utils::bytes_comparator_t>;

    void record(file_info_t &);
    set_t deduce() const;

    file_infos_set_t file_for_removal;
};

} // namespace syncspirit::model
