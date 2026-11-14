// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2026 Ivan Baidakou

#pragma once

#include "task.h"
#include <cstdint>
#include "utils/path.h"
#include <cstdint>

namespace syncspirit::fs::task {

struct child_info_t {
    utils::path_t path;
    utils::path_t target;
    utils::file_type_t file_type;
    std::uint32_t permissions;
    std::int64_t last_write_time;
    std::int64_t size;
    std::error_code ec;
};

} // namespace syncspirit::fs::task