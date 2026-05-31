// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025-2026 Ivan Baidakou

#pragma once

#include "task.h"

namespace syncspirit::fs::task {

struct SYNCSPIRIT_API remove_file_t {
    remove_file_t(bfs::path path) noexcept;
    bool process(fs_slave_t &fs_slave, execution_context_t &context) noexcept;

    bfs::path path;
    sys::error_code ec;
};

} // namespace syncspirit::fs::task
