// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025-2026 Ivan Baidakou

#include "remove_file.h"
#include "fs/messages.h"
#include "utils/path_view.hpp"
#include "fs/fs_proxy.h"

using namespace syncspirit::fs::task;

remove_file_t::remove_file_t(utils::path_t path_) noexcept : path{std::move(path_)} {}

bool remove_file_t::process(fs_slave_t &fs_slave, execution_context_t &context) noexcept {
    auto view = path.get_view(context.allocator);
    ec = context.fs_proxy->remove_file(view);
    if (!ec) {
        return true;
    }
    return false;
}
