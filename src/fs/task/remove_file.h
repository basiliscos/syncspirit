// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include "task.h"

namespace syncspirit::fs::task {

struct SYNCSPIRIT_API remove_file_t {
    remove_file_t(bfs::path path) noexcept;
    void process(fs_slave_t &fs_slave, hasher::hasher_plugin_t *) noexcept;

    bfs::path path;
    sys::error_code ec;
};

} // namespace syncspirit::fs::task
