// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025-2026 Ivan Baidakou

#include "file_cache.h"
#include <cassert>
#include "boost/nowide/convert.hpp"

using namespace syncspirit::fs;

file_ptr_t file_cache_t::get(const utils::path_base_t &path) noexcept {
    assert(path.is_absolute());
    auto key = path.get_full_name();
    return parent_t::get(key);
}
