// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include "model/misc/arc.hpp"
#include "model/misc/path.h"

#include <boost/multi_index/global_fun.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index_container.hpp>

namespace syncspirit::presentation {

struct entity_t;
using entity_ptr_t = model::intrusive_ptr_t<entity_t>;

namespace details {
namespace mi = boost::multi_index;

model::path_t *get_path(const entity_ptr_t &) noexcept;
std::string_view get_parent(const entity_ptr_t &) noexcept;

// clang-format off
using orphans_map_t = mi::multi_index_container<
    entity_ptr_t,
    mi::indexed_by<
        mi::ordered_unique<
            mi::global_fun<const entity_ptr_t&, model::path_t*, get_path>
        >,
        mi::ordered_non_unique<
            mi::global_fun<const entity_ptr_t&, std::string_view, get_parent>
        >
    >
>;
// clang-format on

} // namespace details

struct orphans_t : private details::orphans_map_t {
    ~orphans_t();
    void push(entity_ptr_t) noexcept;
    void reap_children(entity_ptr_t) noexcept;
    entity_ptr_t get_by_path(model::path_t *path) noexcept;
};

} // namespace syncspirit::presentation
