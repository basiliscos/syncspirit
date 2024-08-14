// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#pragma once

#include "arc.hpp"
#include "uuid.h"

#include <random>
#include <mutex>
#include <boost/uuid/random_generator.hpp>

namespace syncspirit::model {

using rng_engine_t = std::mt19937;
using uuid_generator_t = boost::uuids::basic_random_generator<rng_engine_t>;
using uint64_generator_t = std::uniform_int_distribution<uint64_t>;

struct SYNCSPIRIT_API sequencer_t : arc_base_t<sequencer_t> {
    sequencer_t(size_t seed) noexcept;
    sequencer_t(sequencer_t &&) = delete;
    sequencer_t(const sequencer_t &) = delete;

    uuid_t next_uuid() noexcept;
    uint64_t next_uint64() noexcept;

  private:
    using mutex_t = std::mutex;
    using lock_t = std::unique_lock<mutex_t>;
    mutex_t mutex;
    rng_engine_t rng_engine;
    uuid_generator_t uuid_generator;
    uint64_generator_t uint64_generator;
};

using sequencer_ptr_t = intrusive_ptr_t<sequencer_t>;

auto SYNCSPIRIT_API make_sequencer(size_t seed) -> sequencer_ptr_t;

} // namespace syncspirit::model
