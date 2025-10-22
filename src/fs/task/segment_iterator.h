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

namespace bfs = std::filesystem;
namespace sys = boost::system;

struct SYNCSPIRIT_API segment_iterator {
    segment_iterator(bfs::path path, int32_t block_size) noexcept;
    void process(fs_slave_t &fs_slave) noexcept;

    bfs::path path;
    sys::error_code ec;
};

} // namespace task

} // namespace syncspirit::fs
