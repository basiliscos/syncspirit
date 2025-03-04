// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#include "version.h"
#include "device.h"
#include "utils/time.h"
#include <limits>

static constexpr auto undef = std::numeric_limits<size_t>::max();

using namespace syncspirit::model;

// auto now = r::pt::second_clock::local_time();

version_t::version_t(const proto::Vector &v) noexcept {
    assert(v.counters_size());
    counters.resize(v.counters_size());
    auto best = v.counters(0);
    best_index = 0;
    for (int i = 0; i < v.counters_size(); ++i) {
        auto &c = v.counters(i);
        counters[i] = c;
        if (c.value() > best.value()) {
            best = c;
            best_index = i;
        }
    }
}

version_t::version_t(const device_t &device) noexcept : best_index{undef} {
    counters.reserve(1);
    update(device);
}

auto version_t::as_proto() const noexcept -> proto::Vector {
    proto::Vector v;
    to_proto(v);
    return v;
}

void version_t::to_proto(proto::Vector &v) const noexcept {
    v.Clear();
    for (auto &c : counters) {
        *v.add_counters() = c;
    }
}

void version_t::update(const device_t &device) noexcept {
    using clock_t = utils::pt::second_clock;
    auto id = device.device_id().get_uint();
    auto v = static_cast<std::uint64_t>(utils::as_seconds(clock_t::universal_time()));
    auto counter = (proto::Counter *)(nullptr);
    if (best_index != undef) {
        v = std::max(counters[best_index].value() + 1, v);
        best_index = undef;
        for (size_t i = 0; i < counters.size(); ++i) {
            if (counters[i].id() == id) {
                counter = &counters[i];
                best_index = i;
                break;
            }
        }
    }
    if (best_index == undef) {
        counters.emplace_back(proto::Counter());
        counter = &counters.back();
        counter->set_id(id);
        best_index = &counters.back() - counters.data();
    }
    counter->set_value(v);
}

auto version_t::get_best() noexcept -> proto::Counter & {
    assert(best_index != undef);
    return counters[best_index];
}

auto version_t::get_best() const noexcept -> const proto::Counter & {
    assert(best_index != undef);
    return counters[best_index];
}

bool version_t::contains(const version_t &other) noexcept {
    auto &other_best = other.get_best();
    for (size_t i = 0; i < counters.size(); ++i) {
        auto &c = counters[i];
        if (c.id() == other_best.id()) {
            return c.value() >= other_best.value();
        }
    }
    return false;
}

bool version_t::identical_to(const version_t &other) noexcept {
    if (counters.size() == other.counters.size()) {
        auto p1 = counters.data();
        auto p2 = other.counters.data();
        auto end = p1 + counters.size();
        while (p1 != end) {
            if (p1->id() == p2->id() && p1->value() == p2->value()) {
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
