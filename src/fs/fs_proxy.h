// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#pragma once

#include "updates_mediator.h"
#include "utils/io.h"
#include "utils/bytes.h"
#include "syncspirit-export.h"
#include <boost/filesystem.hpp>
#include <boost/outcome.hpp>
#include <cstdint>
#include <filesystem>

namespace syncspirit::fs {

namespace bfs = std::filesystem;
namespace sys = boost::system;
namespace outcome = boost::outcome_v2;
namespace pt = boost::posix_time;

struct SYNCSPIRIT_API fs_proxy_t {
    fs_proxy_t(updates_mediator_t &updates_mediator, const pt::ptime &deadline) noexcept;

    outcome::result<utils::fstream_t> open_write(const bfs::path &path, std::uint64_t file_size) noexcept;
    sys::error_code rename(const bfs::path &from, const bfs::path &to) noexcept;
    sys::error_code remove(const bfs::path &path) noexcept;
    sys::error_code remove_file(const bfs::path &path) noexcept;
    sys::error_code last_write_time(const bfs::path &path, std::int64_t modification_s) noexcept;
    sys::error_code set_perms(const bfs::path &path, std::uint32_t permissions) noexcept;
    sys::error_code create_link(const bfs::path &target, const bfs::path &path) noexcept;
    sys::error_code create_directories(const bfs::path &path) noexcept;
    sys::error_code write(const bfs::path &path, utils::fstream_t &stream, utils::bytes_view_t data) noexcept;

    pt::ptime deadline;
    updates_mediator_t &updates_mediator;
    std::uint_fast32_t mediator_updates = 0;
};

} // namespace syncspirit::fs
