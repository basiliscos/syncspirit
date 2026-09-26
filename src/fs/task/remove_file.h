// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025-2026 Ivan Baidakou

#pragma once

#include "task.h"

namespace syncspirit::fs::task {

struct SYNCSPIRIT_API remove_file_t {
    remove_file_t(utils::path_t path) noexcept;
    bool process(fs_slave_t &fs_slave, execution_context_t &context) noexcept;

    utils::path_t path;
    std::error_code ec;
};

} // namespace syncspirit::fs::task
