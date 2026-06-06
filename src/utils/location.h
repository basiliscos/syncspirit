// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2026 Ivan Baidakou

#pragma once

#include <string>
#include "syncspirit-export.h"
#include "path_view.hpp"

namespace syncspirit::utils {

SYNCSPIRIT_API poly_path_view_t get_home_dir(const allocator_t &) noexcept;

SYNCSPIRIT_API poly_path_view_t get_default_config_dir(const allocator_t &) noexcept;

SYNCSPIRIT_API poly_path_view_t expand_home(const std::string &path, const poly_path_view_t &home) noexcept;

} // namespace syncspirit::utils
