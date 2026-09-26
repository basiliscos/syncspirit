// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025-2026 Ivan Baidakou

#pragma once

#include "task.h"
#include <cstdint>

namespace syncspirit::fs::task {

struct SYNCSPIRIT_API rename_file_t {
    rename_file_t(utils::path_t path, utils::path_t new_name, std::int64_t modification_s,
                  hasher::payload::extendended_context_prt_t context) noexcept;
    bool process(fs_slave_t &fs_slave, execution_context_t &context) noexcept;

    utils::path_t path;
    utils::path_t new_name;
    std::int64_t modification_s;
    std::error_code ec;
    hasher::payload::extendended_context_prt_t context;
};

} // namespace syncspirit::fs::task
