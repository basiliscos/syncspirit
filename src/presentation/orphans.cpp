// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "orphans.h"
#include "entity.h"
#include <deque>
#include <memory_resource>

namespace syncspirit::presentation {

namespace details {

std::string_view get_path(const entity_ptr_t &entity) noexcept { return entity->get_path().get_full_name(); }

std::string_view get_parent(const entity_ptr_t &entity) noexcept { return entity->get_path().get_parent_name(); }

} // namespace details

orphans_t::~orphans_t() {}

void orphans_t::push(entity_ptr_t entity) noexcept { emplace(std::move(entity)); }

void orphans_t::reap_children(entity_ptr_t parent) noexcept {
    auto buffer = std::array<std::byte, 256>();
    auto pool = std::pmr::monotonic_buffer_resource(buffer.data(), buffer.size());
    using queue_t = std::pmr::deque<entity_t *>;
    auto allocator = std::pmr::polymorphic_allocator<entity_t *>(&pool);

    auto &by_parent_name = get<1>();
    auto &by_name = get<0>();
    auto queue = queue_t(allocator);
    queue.emplace_back(parent.get());

    while (!queue.empty()) {
        auto item = queue.front();
        queue.pop_front();

        auto item_name = item->get_path().get_full_name();
        auto [b, e] = by_parent_name.equal_range(item_name);
        for (auto it = b; it != e; ++it) {
            auto &child = **it;
            queue.emplace_back(&child);

            auto it_name = by_name.find(child.get_path().get_full_name());
            // by_name.erase(it_name);

            item->add_child(child);
        }
        by_parent_name.erase(b, e);
    }
}

entity_ptr_t orphans_t::get_by_path(std::string_view path) noexcept {
    auto &by_name = get<0>();
    auto it = by_name.find(path);
    if (it != by_name.end()) {
        return *it;
    }
    return {};
}

} // namespace syncspirit::presentation
