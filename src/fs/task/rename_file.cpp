// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025-2026 Ivan Baidakou

#include "rename_file.h"
#include "fs/fs_proxy.h"

using namespace syncspirit::fs::task;

rename_file_t::rename_file_t(bfs::path path_, bfs::path new_name_, std::int64_t modification_s_,
                             hasher::payload::extendended_context_prt_t context_) noexcept
    : path{std::move(path_)}, new_name{std::move(new_name_)}, modification_s{modification_s_},
      context{std::move(context_)} {}

bool rename_file_t::process(fs_slave_t &fs_slave, execution_context_t &context) noexcept {
    auto new_path = path.parent_path() / new_name;
    ec = context.fs_proxy->rename(path, new_path);
    if (!ec) {
        ec = context.fs_proxy->last_write_time(new_path, modification_s);
        return true;
    }
    return false;
}
