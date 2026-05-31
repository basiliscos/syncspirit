// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025-2026 Ivan Baidakou

#include "remove_file.h"
#include "fs/messages.h"
#include "fs/fs_proxy.h"

using namespace syncspirit::fs::task;

remove_file_t::remove_file_t(bfs::path path_) noexcept : path{std::move(path_)} {}

bool remove_file_t::process(fs_slave_t &fs_slave, execution_context_t &context) noexcept {
    ec = context.fs_proxy->remove_file(path);
    if (!ec) {
        return true;
    }
    return false;
}
