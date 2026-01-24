// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025-2026 Ivan Baidakou

#pragma once

#include "task.h"
#include <cstdint>

namespace syncspirit::fs::task {

struct SYNCSPIRIT_API rename_file_t {
    rename_file_t(bfs::path path, bfs::path new_name, std::int64_t modification_s,
                  hasher::payload::extendended_context_prt_t context) noexcept;
    bool process(fs_slave_t &fs_slave, execution_context_t &context) noexcept;

    bfs::path path;
    bfs::path new_name;
    std::int64_t modification_s;
    sys::error_code ec;
    hasher::payload::extendended_context_prt_t context;
};

} // namespace syncspirit::fs::task
