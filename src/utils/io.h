// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025-2026 Ivan Baidakou

#pragma once

#include "syncspirit-export.h"
#include "utils/path.h"
#include <cstdint>
#include <string_view>
#include <boost/outcome.hpp>
#include "utils/bytes.h"

namespace syncspirit::utils {

namespace outcome = boost::outcome_v2;

namespace details {
struct open_write_result_t;
};

struct SYNCSPIRIT_API io_stream_t {
    using offset_t = std::uint64_t;
    using opne_write_t = outcome::result<details::open_write_result_t>;

    io_stream_t() = default;
    io_stream_t(io_stream_t &&) noexcept;
    ~io_stream_t();

    static outcome::result<io_stream_t> open_truncate(const utils::poly_path_view_t &path) noexcept;
    static opne_write_t open_write(const utils::poly_path_view_t &path, std::size_t size) noexcept;
    static outcome::result<io_stream_t> open_read(const utils::poly_path_view_t &path) noexcept;

    outcome::result<void> close() noexcept;

    outcome::result<offset_t> get_position() const noexcept;
    outcome::result<void> set_position(offset_t) noexcept;

    outcome::result<offset_t> get_size() noexcept;

    outcome::result<void> read(unsigned char *ptr, std::size_t number) noexcept;
    outcome::result<bytes_t> read_bytes(offset_t size) noexcept;
    outcome::result<bytes_t> read_whole() noexcept;

    outcome::result<void> write(unsigned const char *ptr, std::size_t number) noexcept;
    outcome::result<void> write(std::string_view) noexcept;

  private:
    io_stream_t(int fd) noexcept;
    int fd = -1;
};

namespace details {

struct open_write_result_t {
    io_stream_t stream;
    bool resized;
    bool created;
};

} // namespace details

} // namespace syncspirit::utils
