// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025-2026 Ivan Baidakou

#pragma once

#include "syncspirit-export.h"
#include <filesystem>
#include <optional>
#include <cstdint>
#include <string_view>

namespace syncspirit::utils {

namespace bfs = std::filesystem;

namespace details {
struct open_write_result_t;
};

struct SYNCSPIRIT_API io_stream_t {
    using offset_t = std::uint64_t;
    using offset_opt_t = std::optional<offset_t>;
    using content_opt_t = std::optional<std::string>;

    io_stream_t() = default;
    ~io_stream_t();

    io_stream_t(io_stream_t &&) noexcept;
    static io_stream_t open_truncate(const bfs::path &path) noexcept;
    static details::open_write_result_t open_write(const bfs::path &path, std::size_t size) noexcept;
    static io_stream_t open_read(const bfs::path &path) noexcept;

    offset_opt_t get_position() const noexcept;
    bool set_position(offset_t) noexcept;
    offset_opt_t get_size() noexcept;
    bool read(unsigned char *ptr, std::size_t number) noexcept;

    content_opt_t read_whole() noexcept;
    bool write(unsigned const char *ptr, std::size_t number) noexcept;
    bool write(std::string_view) noexcept;
    bool close() noexcept;

    operator bool() const noexcept;

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
