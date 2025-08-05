// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include "file.h"
#include "model/misc/lru_cache.hpp"
#include "model/misc/arc.hpp"
#include "syncspirit-export.h"

namespace syncspirit::model::details {

template <> inline std::string_view get_lru_key<syncspirit::fs::file_ptr_t>(const fs::file_ptr_t &item) {
    return item->get_path_view();
}

} // namespace syncspirit::model::details

namespace syncspirit::fs {

struct SYNCSPIRIT_API file_cache_t : model::arc_base_t<file_cache_t>, private model::mru_list_t<file_ptr_t> {
    using parent_t = model::mru_list_t<file_ptr_t>;

    using parent_t::parent_t;

    using parent_t::clear;
    using parent_t::get_max_items;
    using parent_t::put;
    using parent_t::remove;

    file_ptr_t get(const bfs::path &path) noexcept;
};

using file_cache_ptr_t = model::intrusive_ptr_t<file_cache_t>;

} // namespace syncspirit::fs
