// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include "cluster_diff.h"
#include "utils/bytes.h"

namespace syncspirit::model {

struct file_info_t;
struct folder_info_t;

namespace diff {

struct SYNCSPIRIT_API block_diff_t : cluster_diff_t {
    block_diff_t(const block_diff_t &) noexcept;
    block_diff_t(const file_info_t &file, const folder_info_t &fi, std::uint32_t block_index = 0) noexcept;
    block_diff_t(std::string file_name, std::string folder_id, utils::bytes_t device_id, utils::bytes_t block_hash,
                 std::uint32_t block_index) noexcept;

    std::string file_name;
    std::string folder_id;
    utils::bytes_t device_id;
    utils::bytes_t block_hash;
    std::uint32_t block_index;
};

} // namespace diff
} // namespace syncspirit::model
