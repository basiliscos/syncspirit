// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once
#include <cstdint>

namespace syncspirit::config {

struct fs_config_t {
    std::uint32_t temporally_timeout;
    std::uint32_t mru_size;
    std::int64_t bytes_scan_iteration_limit;
    std::int64_t files_scan_iteration_limit;
};

} // namespace syncspirit::config
