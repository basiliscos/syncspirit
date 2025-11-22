// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "rename_file.h"
#include "fs/utils.h"

using namespace syncspirit::fs::task;

rename_file_t::rename_file_t(bfs::path path_, bfs::path new_name_, std::int64_t modification_s_,
                             hasher::payload::extendended_context_prt_t context_) noexcept
    : path{std::move(path_)}, new_name{std::move(new_name_)}, modification_s{modification_s_},
      context{std::move(context_)} {}

void rename_file_t::process(fs_slave_t &fs_slave, hasher::hasher_plugin_t *) noexcept {
    ec = {};
    auto new_path = path.parent_path() / new_name;
    bfs::rename(path, new_path, ec);
    if (!ec) {
        auto modified = from_unix(modification_s);
        bfs::last_write_time(new_path, modified, ec);
    }
}
