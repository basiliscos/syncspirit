// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#include "sequencer.h"

namespace syncspirit::model {

sequencer_t::sequencer_t(size_t seed) noexcept : uuid_generator(rng_engine) { rng_engine.seed(seed); }

bu::uuid sequencer_t::next_uuid() noexcept {
    auto lock = lock_t(mutex);
    return uuid_generator();
}

uint64_t sequencer_t::next_uint64() noexcept {
    auto lock = lock_t(mutex);
    return uint64_generator(rng_engine);
}

auto make_sequencer(size_t seed) -> sequencer_ptr_t { return new sequencer_t(seed); }

} // namespace syncspirit::model
