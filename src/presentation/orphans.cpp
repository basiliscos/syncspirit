// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "orphans.h"
#include "entity.h"
#include <list>

namespace syncspirit::presentation {

namespace details {

std::string_view get_path(const entity_ptr_t &entity) { return entity->get_path().get_full_name(); }

std::string_view get_parent(const entity_ptr_t &entity) { return entity->get_path().get_parent_name(); }

} // namespace details

orphans_t::~orphans_t() {}

void orphans_t::push(entity_ptr_t entity) { emplace(std::move(entity)); }

void orphans_t::reap_children(entity_ptr_t parent) {
    using queue_t = std::list<entity_t *>;
    auto &by_parent_name = get<1>();
    auto &by_name = get<0>();
    auto queue = queue_t();
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

} // namespace syncspirit::presentation
