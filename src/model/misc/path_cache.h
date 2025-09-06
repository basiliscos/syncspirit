// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include "path.h"
#include <unordered_map>

namespace syncspirit::model {

struct SYNCSPIRIT_API path_cache_t : arc_base_t<path_cache_t> {
    using map_t = std::unordered_map<std::string_view, path_t *>;

    path_ptr_t get_path(std::string_view name) noexcept;

    map_t map;
};

using path_cache_ptr_t = intrusive_ptr_t<path_cache_t>;

} // namespace syncspirit::model
