// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#include "version.h"
#include "device.h"
#include "proto/proto-helpers.h"
#include "utils/time.h"

using namespace syncspirit::model;

static constexpr std::uint64_t BEST_MASK = 1ull << 63;

version_t::version_t() noexcept {}

version_t::version_t(const proto::Vector &v) noexcept {
    auto counters_sz = proto::get_counters_size(v);
    assert(counters_sz);
    counters.resize(counters_sz);
    auto best = proto::get_counters(v, 0);
    auto best_index = 0;
    for (int i = 0; i < counters_sz; ++i) {
        auto &c = proto::get_counters(v, i);
        counters[i] = c;
        if (proto::get_value(c) > proto::get_value(best)) {
            best = c;
            best_index = i;
        }
    }

    auto &selected = counters[best_index];
    auto marked = proto::get_value(selected) | BEST_MASK;
    proto::set_value(selected, marked);
}

version_t::version_t(const device_t &device) noexcept { update(device); }

auto version_t::as_proto() const noexcept -> proto::Vector {
    proto::Vector v;
    to_proto(v);
    return v;
}

void version_t::to_proto(proto::Vector &v) const noexcept {
    proto::clear_counters(v);
    for (auto &c : counters) {
        auto id = proto::get_id(c);
        auto value = proto::get_value(c);
        proto::add_counters(v, proto::Counter(id, value & ~BEST_MASK));
    }
}

void version_t::update(const device_t &device) noexcept {
    using clock_t = utils::pt::second_clock;
    auto id = device.device_id().get_uint();
    auto v = static_cast<std::uint64_t>(utils::as_seconds(clock_t::universal_time()));
    auto counter = (proto::Counter *)(nullptr);
    int best_index = -1;
    for (auto &c : counters) {
        auto value = proto::get_value(c);
        if (value & BEST_MASK) {
            v = std::max((proto::get_value(c) & ~BEST_MASK) + 1, v);
            for (size_t i = 0; i < counters.size(); ++i) {
                if (proto::get_id(counters[i]) == id) {
                    counter = &counters[i];
                    best_index = i;
                    break;
                }
            }
        }
    }
    if (best_index < 0) {
        auto copy = proto::Counter(id, v | BEST_MASK);
        auto new_sz = counters.size();
        counters.resize(new_sz + 1);
        counters[new_sz] = std::move(copy);
        best_index = new_sz;
    } else {
        assert(counter);
        proto::set_value(*counter, v | BEST_MASK);
    }
}

auto version_t::get_best() const noexcept -> proto::Counter {
    auto r = proto::Counter();
    for (auto &c : counters) {
        auto value = proto::get_value(c);
        if (value & BEST_MASK) {
            r = proto::Counter{proto::get_id(c), value & ~BEST_MASK};
        }
    }
    return r;
}

bool version_t::contains(const version_t &other) const noexcept {
    auto other_best = other.get_best();
    for (size_t i = 0; i < counters.size(); ++i) {
        auto &c = counters[i];
        auto id_my = proto::get_id(c);
        auto id_other = proto::get_id(other_best);
        auto ids_match = id_my == id_other;
        if (ids_match) {
            auto value_my = proto::get_value(c) & ~BEST_MASK;
            auto value_other = proto::get_value(other_best);
            return value_my >= value_other;
        }
    }
    return false;
}

bool version_t::identical_to(const version_t &other) const noexcept {
    if (counters.size() == other.counters.size()) {
        auto p1 = counters.data();
        auto p2 = other.counters.data();
        auto end = p1 + counters.size();
        while (p1 != end) {
            auto ids_match = proto::get_id(*p1) == proto::get_id(*p2);
            auto v1 = proto::get_value(*p1) & ~BEST_MASK;
            auto v2 = proto::get_value(*p2) & ~BEST_MASK;
            auto values_match = v1 == v2;
            if (ids_match && values_match) {
                ++p1;
                ++p2;
                continue;
            }
            return false;
        }
        return true;
    }
    return false;
}

size_t version_t::counters_size() const { return counters.size(); }

auto version_t::get_counter(size_t index) noexcept -> const proto::Counter & {
    assert(index <= counters.size());
    return counters[index];
}

auto version_t::get_counters() const noexcept -> counters_t {
    auto copy = counters_t();
    copy.resize(counters.size());
    for (std::uint32_t i = 0; i < counters.sz; ++i) {
        auto id = proto::get_id(counters[i]);
        auto value = proto::get_value(counters[i]) & ~BEST_MASK;
        copy[i] = proto::Counter(id, value);
    }
    return counters;
}
