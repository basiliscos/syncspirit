// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include "task.h"
#include "presentation/presence.h"

namespace syncspirit::fs::task {

struct SYNCSPIRIT_API scan_dir_t {
    struct child_info_t {
        bfs::path path;
        bfs::path target;
        bfs::file_status status;
        bfs::file_time_type last_write_time = {};
        std::uintmax_t size;
        sys::error_code ec;
    };
    using child_infos_t = std::vector<child_info_t>;

    scan_dir_t(bfs::path path, presentation::presence_ptr_t presence) noexcept;
    void process(fs_slave_t &fs_slave, hasher::hasher_plugin_t *) noexcept;

    bfs::path path;
    presentation::presence_ptr_t presence;
    sys::error_code ec;
    child_infos_t child_infos;
};

} // namespace syncspirit::fs::task
