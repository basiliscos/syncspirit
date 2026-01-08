// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include <rotor.hpp>
#include <rotor/plugin/registry.h>
#include <vector>
#include <cstdint>
#include "syncspirit-export.h"
#include "utils/bytes.h"
#include "messages.h"

namespace syncspirit {
namespace hasher {

namespace r = rotor;

struct SYNCSPIRIT_API hasher_plugin_t : public r::plugin::registry_plugin_t {
    using parent_t = r::plugin::registry_plugin_t;
    using parent_t::parent_t;

    /** The plugin unique identity to allow further static_cast'ing*/
    static const std::type_index class_identity;

    const std::type_index &identity() const noexcept override;

    void configure_hashers(std::uint32_t number) noexcept;

    bool handle_init(r::message::init_request_t *message) noexcept override;

    void calc_digest(utils::bytes_t data, std::int32_t block_index, const r::address_ptr_t &reply_back,
                     payload::extendended_context_prt_t context = {}) noexcept;

  private:
    using hashers_t = std::vector<r::address_ptr_t>;
    using usages_t = std::vector<std::int32_t>;
    hashers_t hashers;
    usages_t usages;
    std::int32_t min_usage = 0;
};

} // namespace hasher
} // namespace syncspirit
