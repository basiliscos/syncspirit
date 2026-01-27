// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "updates_mediator.h"

#include <algorithm>
#include <memory_resource>
#include <fmt/ranges.h>

using namespace syncspirit::fs;

static constexpr size_t MAX_LOG_ITEMS = 5;

updates_mediator_t::updates_mediator_t(const pt::time_duration &interval_) : interval{interval_} {
    log = utils::get_logger("fs.updates_mediator");
}

void updates_mediator_t::push(std::string path, std::string prev_path, const timepoint_t &deadline) noexcept {
    auto target = (updates_t *){};
    auto counter = update_type_internal_t{1};
    if (next.deadline == deadline) {
        target = &next;
    } else if (next.deadline.is_not_a_date_time()) {
        target = &next;
        next.deadline = deadline;
    } else {
        target = &postponed;
        auto it = next.updates.find(path);
        if (it != next.updates.end()) {
            counter += it->update_type;
            next.updates.erase(it);
        }
        if (target->updates.empty()) {
            target->deadline = next.deadline + interval;
        }
    }

    auto update = support::file_update_t(std::move(path), std::move(prev_path), counter);
    auto [it, inserted] = target->updates.emplace(std::move(update));
    if (!inserted) {
        it->update_type += counter;
    }
}

bool updates_mediator_t::is_masked(std::string_view path) noexcept {
    for (auto target : {&postponed, &next}) {
        if (!target->deadline.is_not_a_date_time()) {
            auto &updates = target->updates;
            auto it = updates.find(path);
            if (it != updates.end()) {
                --it->update_type;
                if (!it->update_type) {
                    updates.erase(it);
                }
                return true;
            }
        }
    }
    return false;
}

bool updates_mediator_t::clean_expired() noexcept {
    next.deadline = {};
    auto &expired = next.updates;
    if (expired.size()) {
        using item_t = std::string_view;
        using items_t = std::pmr::vector<item_t>;
        auto buffer = std::array<std::byte, 1024>();
        auto pool = std::pmr::monotonic_buffer_resource(buffer.data(), buffer.size());
        auto allocator = std::pmr::polymorphic_allocator<char>(&pool);
        auto items = items_t(allocator);
        auto i = 0;
        for (auto it = expired.begin(); it != expired.end() && i < MAX_LOG_ITEMS; ++it, ++i) {
            items.emplace_back(it->path);
        }
        LOG_DEBUG(log, "cleaning {} expired items: {}...", expired.size(), fmt::join(items, ", "));
        expired.clear();
    }
    std::swap(next, postponed);
    return next.updates.size();
}
