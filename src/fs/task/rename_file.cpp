// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025-2026 Ivan Baidakou

#include "rename_file.h"
#include "utils/path_view.hpp"
#include "fs/fs_proxy.h"

using namespace syncspirit::fs::task;

rename_file_t::rename_file_t(utils::path_t path_, utils::path_t new_name_, std::int64_t modification_s_,
                             hasher::payload::extendended_context_prt_t context_) noexcept
    : path{std::move(path_)}, new_name{std::move(new_name_)}, modification_s{modification_s_},
      context{std::move(context_)} {}

bool rename_file_t::process(fs_slave_t &fs_slave, execution_context_t &context) noexcept {
    auto parent = path.get_view(context.allocator).get_parent();
    auto name = new_name.get_view(context.allocator);
    auto new_path = parent / name;
    ec = context.fs_proxy->rename(path, new_path);
    if (!ec) {
        ec = context.fs_proxy->last_write_time(new_path, modification_s);
        return true;
    }
    return false;
}
