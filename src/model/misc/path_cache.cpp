// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "path_cache.h"

using namespace syncspirit::model;

namespace {

struct cached_path_t : path_t {
    cached_path_t(std::string_view full_name, path_cache_t &cache_) noexcept : path_t(full_name), cache{cache_} {
        intrusive_ptr_add_ref(&cache);
    }

    ~cached_path_t() {
        cache.map.erase(get_full_name());
        intrusive_ptr_release(&cache);
    }

    path_cache_t &cache;
};

} // namespace

auto path_cache_t::get_path(std::string_view name) noexcept -> path_ptr_t {
    assert(name.size());
    auto it = map.find(name);
    if (it == map.end()) {
        auto ptr = path_ptr_t(new cached_path_t(name, *this));
        map.emplace(ptr->get_full_name(), ptr.get());
        return ptr;
    }
    return it->second;
}
