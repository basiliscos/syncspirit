// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#include "version.h"
#include "device.h"
#include "proto/proto-helpers.h"
#include "utils/time.h"
#include <limits>

static constexpr auto undef = std::numeric_limits<uint32_t>::max();

using namespace syncspirit::model;

version_t::version_t() noexcept : best_index{undef} {}

version_t::version_t(const proto::Vector &v) noexcept {
    auto counters_sz = proto::get_counters_size(v);
    assert(counters_sz);
    counters.resize(counters_sz);
    auto best = proto::get_counters(v, 0);
    best_index = 0;
    for (int i = 0; i < counters_sz; ++i) {
        auto &c = proto::get_counters(v, i);
        counters[i] = c;
        if (proto::get_value(c) > proto::get_value(best)) {
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
    proto::clear_counters(v);
    for (auto &c : counters) {
        proto::add_counters(v, c);
    }
}

void version_t::update(const device_t &device) noexcept {
    using clock_t = utils::pt::second_clock;
    auto id = device.device_id().get_uint();
    auto v = static_cast<std::uint64_t>(utils::as_seconds(clock_t::universal_time()));
    auto counter = (proto::Counter *)(nullptr);
    if (best_index != undef) {
        v = std::max(proto::get_value(counters[best_index]) + 1, v);
        best_index = undef;
        for (size_t i = 0; i < counters.size(); ++i) {
            if (proto::get_id(counters[i]) == id) {
                counter = &counters[i];
                best_index = i;
                break;
            }
        }
    }
    if (best_index == undef) {
        counters.emplace_back(proto::Counter());
        counter = &counters.back();
        proto::set_id(*counter, id);
        best_index = &counters.back() - counters.data();
    }
    proto::set_value(*counter, v);
}

auto version_t::get_best() noexcept -> proto::Counter & {
    assert(best_index != undef);
    return counters[best_index];
}

auto version_t::get_best() const noexcept -> const proto::Counter & {
    assert(best_index != undef);
    return counters[best_index];
}

bool version_t::contains(const version_t &other) const noexcept {
    auto &other_best = other.get_best();
    for (size_t i = 0; i < counters.size(); ++i) {
        auto &c = counters[i];
        auto ids_match = proto::get_id(c) == proto::get_id(other_best);
        if (ids_match) {
            return proto::get_value(c) >= proto::get_value(other_best);
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
            auto values_match = proto::get_value(*p1) == proto::get_value(*p2);
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

auto version_t::get_counters() noexcept -> const counters_t & { return counters; }
