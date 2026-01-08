// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include "task.h"

namespace syncspirit::fs::task {

struct SYNCSPIRIT_API noop_t {
    noop_t() noexcept = default;
    void process(fs_slave_t &fs_slave, hasher::hasher_plugin_t *) noexcept;
};

} // namespace syncspirit::fs::task
