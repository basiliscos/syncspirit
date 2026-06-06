// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#pragma once

#include "path.h"
#include <system_error>
#include "syncspirit-export.h"

namespace syncspirit::utils {

SYNCSPIRIT_API bool exists(const poly_path_view_t &path, std::error_code &ec) noexcept;
SYNCSPIRIT_API bool is_empty(const poly_path_view_t &path, std::error_code &ec) noexcept;
SYNCSPIRIT_API std::size_t create_directories(const poly_path_view_t &path, std::error_code &ec) noexcept;

} // namespace syncspirit::utils
