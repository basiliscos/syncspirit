// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "updates_mediator.h"

#include <algorithm>
#include <memory_resource>
#include <fmt/ranges.h>
#include <type_traits>
#include <boost/nowide/convert.hpp>

using namespace syncspirit::fs;
using boost::nowide::narrow;

static constexpr size_t MAX_LOG_ITEMS = 5;

updates_mediator_t::updates_mediator_t(const pt::time_duration &interval_, bool enabled_)
    : interval{interval_}, enabled{enabled_} {
    log = utils::get_logger("fs.updates_mediator");
}

template <typename T> struct Stringizer;

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
template <> struct Stringizer<wchar_t> {
    static std::string get(const bfs::path &path) { return narrow(path.generic_wstring()); }
};
#else
template <> struct Stringizer<char> {
    static std::string get(const bfs::path &path) { return path.native(); }
};
#endif

void updates_mediator_t::mask(const bfs::path &path, const bfs::path &prev_path, const timepoint_t &deadline) noexcept {
    using result_t = decltype(path.native());
    using native_type_t = std::remove_cv_t<std::remove_reference_t<result_t>>;
    using char_t = typename native_type_t::value_type;

    if (!enabled) {
        return;
    }

    auto target = (updates_t *){};
    auto counter = update_type_internal_t{1};
    auto path_str = Stringizer<char_t>::get(path);
    auto prev_path_str = Stringizer<char_t>::get(prev_path);
    if (next.deadline == deadline) {
        target = &next;
    } else if (next.deadline.is_not_a_date_time()) {
        target = &next;
        next.deadline = deadline;
    } else {
        target = &postponed;
        auto it = next.updates.find(path_str);
        if (it != next.updates.end()) {
            counter += it->update_type;
            next.updates.erase(it);
        }
        if (target->updates.empty()) {
            target->deadline = next.deadline + interval;
        }
    }

    auto update = support::file_update_t(std::move(path_str), std::move(prev_path_str), counter);
    auto [it, inserted] = target->updates.emplace(std::move(update));
    if (!inserted) {
        it->update_type += counter;
    }
}

std::uint32_t updates_mediator_t::is_masked(std::string_view path) noexcept {
    for (auto target : {&postponed, &next}) {
        if (!target->deadline.is_not_a_date_time()) {
            auto &updates = target->updates;
            auto it = updates.find(path);
            if (it != updates.end()) {
                auto events = it->update_type--;
                if (!it->update_type) {
                    updates.erase(it);
                }
                return events;
            }
        }
    }
    return 0;
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

void updates_mediator_t::enable(bool value) noexcept { enabled = value; }
