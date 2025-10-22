// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include "syncspirit-export.h"
#include <filesystem>
#include <boost/system.hpp>

namespace syncspirit::fs {

namespace bfs = std::filesystem;
namespace sys = boost::system;

struct fs_slave_t;

namespace task {

struct SYNCSPIRIT_API scan_dir_t {
    struct child_info_t {
        bfs::path path;
        bfs::path target;
        bfs::file_status status;
        bfs::file_time_type last_write_time;
        std::uintmax_t size;
        sys::error_code ec;
    };
    using child_infos_t = std::vector<child_info_t>;

    scan_dir_t(bfs::path path) noexcept;
    void process(fs_slave_t &fs_slave) noexcept;

    bfs::path path;
    sys::error_code ec;
    child_infos_t child_infos;
};

} // namespace task
} // namespace syncspirit::fs
