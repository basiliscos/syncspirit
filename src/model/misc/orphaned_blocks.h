// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#pragma once

#include "model/file_info.h"
#include "utils/string_comparator.hpp"
#include <set>

namespace syncspirit::model {

struct SYNCSPIRIT_API orphaned_blocks_t {
    using set_t = std::set<std::string, utils::string_comparator_t>;

    void record(file_info_t &);
    set_t deduce() const;

    file_infos_set_t file_for_removal;
};

} // namespace syncspirit::model
