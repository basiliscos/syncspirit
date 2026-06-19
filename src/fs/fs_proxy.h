// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#pragma once

#include "updates_mediator.h"
#include "utils/io.h"
#include "utils/bytes.h"
#include "syncspirit-export.h"
#include <boost/outcome.hpp>
#include <cstdint>

namespace syncspirit::fs {

namespace sys = boost::system;
namespace outcome = boost::outcome_v2;
namespace pt = boost::posix_time;

struct SYNCSPIRIT_API fs_proxy_t {
    fs_proxy_t(updates_mediator_t &updates_mediator, const pt::ptime &deadline) noexcept;

    outcome::result<utils::io_stream_t> open_write(const utils::poly_path_view_t &path,
                                                   std::uint64_t file_size) noexcept;
    sys::error_code rename(const utils::path_base_t &from, const utils::poly_path_view_t &to) noexcept;
    sys::error_code last_write_time(const utils::poly_path_view_t &path, std::int64_t modification_s) noexcept;
    sys::error_code remove(const utils::poly_path_view_t &path) noexcept;
    sys::error_code remove_file(const utils::poly_path_view_t &path) noexcept;
    sys::error_code write(const utils::path_base_t &path, utils::io_stream_t &stream,
                          utils::bytes_view_t data) noexcept;
    sys::error_code create_directories(const utils::poly_path_view_t &path) noexcept;
    sys::error_code set_perms(const utils::poly_path_view_t &path, std::uint32_t permissions) noexcept;
    sys::error_code create_link(const utils::path_base_t &target, const utils::path_base_t &path) noexcept;

    pt::ptime deadline;
    updates_mediator_t &updates_mediator;
    std::uint_fast32_t mediator_updates = 0;
};

} // namespace syncspirit::fs
