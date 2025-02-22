// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include "cluster_diff.h"

namespace syncspirit::model {

struct file_info_t;

namespace diff {

struct SYNCSPIRIT_API block_diff_t : cluster_diff_t {
    block_diff_t(const block_diff_t &) noexcept;
    block_diff_t(const file_info_t &file, size_t block_index = 0) noexcept;

    std::string file_name;
    std::string folder_id;
    std::string device_id;
    std::string block_hash;
    size_t block_index;
};

} // namespace diff
} // namespace syncspirit::model
