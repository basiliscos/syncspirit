// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025-2025 Ivan Baidakou

#pragma once

#include "task.h"

namespace syncspirit::fs::task {

struct SYNCSPIRIT_API noop_t {
    noop_t() noexcept = default;
    bool process(fs_slave_t &fs_slave, execution_context_t &context) noexcept;
};

} // namespace syncspirit::fs::task
