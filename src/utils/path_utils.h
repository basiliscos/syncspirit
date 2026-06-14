// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#pragma once

#include "path.h"
#include <system_error>
#include <cstdint>
#include "syncspirit-export.h"

namespace syncspirit::utils {

struct stats_t {
    std::int64_t file_size{0};
    std::int64_t modification{0};
    std::uint32_t permissions{0};
    file_type_t file_type;
    bool supported{false};
};

SYNCSPIRIT_API bool exists(const poly_path_view_t &path, std::error_code &ec) noexcept;
SYNCSPIRIT_API std::int64_t last_write_time(const poly_path_view_t &path, std::error_code &ec) noexcept;
SYNCSPIRIT_API void last_write_time(const poly_path_view_t &path, std::int64_t, std::error_code &ec) noexcept;
SYNCSPIRIT_API bool is_empty(const poly_path_view_t &path, std::error_code &ec) noexcept;
SYNCSPIRIT_API void remove_all(const poly_path_view_t &path, std::error_code &ec) noexcept;
SYNCSPIRIT_API void remove_file(const poly_path_view_t &path, std::error_code &ec) noexcept;
SYNCSPIRIT_API void rename(const utils::path_base_t &from, const utils::poly_path_view_t &to, std::error_code &ec) noexcept;
SYNCSPIRIT_API void chmod(const utils::path_base_t &path, std::uint32_t, std::error_code &ec) noexcept;
SYNCSPIRIT_API void create_symlink(const utils::path_base_t &target, const utils::path_base_t &path, std::error_code &ec) noexcept;
SYNCSPIRIT_API bool is_symlink(const utils::path_base_t &target, std::error_code &ec) noexcept;
SYNCSPIRIT_API poly_string_t read_symlink(const poly_path_view_t &target, std::error_code &ec) noexcept;
SYNCSPIRIT_API std::size_t create_directories(const poly_path_view_t &path, std::error_code &ec) noexcept;
SYNCSPIRIT_API stats_t get_stats(const poly_path_view_t &path, std::error_code &ec) noexcept;

} // namespace syncspirit::utils
