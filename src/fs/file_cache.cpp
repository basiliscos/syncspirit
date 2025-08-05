// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "file_cache.h"
#include <cassert>
#include "boost/nowide/convert.hpp"

using namespace syncspirit::fs;

file_ptr_t file_cache_t::get(const bfs::path &path) noexcept {
    assert(path.is_absolute());
    auto key = boost::nowide::narrow(path.generic_wstring());
    return parent_t::get(key);
}
