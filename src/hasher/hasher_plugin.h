// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include <rotor.hpp>
#include <vector>
#include <cstdint>
#include "syncspirit-export.h"
#include "utils/bytes.h"

namespace syncspirit {
namespace hasher {

namespace r = rotor;

struct SYNCSPIRIT_API hasher_plugin_t : public r::plugin::plugin_base_t {
    using plugin_base_t::plugin_base_t;

    /** The plugin unique identity to allow further static_cast'ing*/
    static const std::type_index class_identity;

    const std::type_index &identity() const noexcept override;

    void activate(r::actor_base_t *actor) noexcept override;

    void configure(std::uint32_t number_of_hashers) noexcept;

    bool handle_init(r::message::init_request_t *message) noexcept override;

    void calc_digest(utils::bytes_t data, std::int32_t block_index) noexcept;

  private:
    using hashers_t = std::vector<r::address_ptr_t>;
    using usages_t = std::vector<std::int32_t>;
    hashers_t hashers;
    usages_t usages;
    std::int32_t min_usage = 0;
};

} // namespace hasher
} // namespace syncspirit
