// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "hasher_plugin.h"
#include "messages.h"
#include <fmt/format.h>
#include <numeric>

using namespace rotor;
using namespace syncspirit::hasher;

namespace {
namespace to {
struct get_plugin {};
} // namespace to
} // namespace

template <>
auto actor_base_t::access<to::get_plugin, const std::type_index *>(const std::type_index *identity_) noexcept {
    return get_plugin(*identity_);
}

const std::type_index hasher_plugin_t::class_identity = typeid(hasher_plugin_t);

const std::type_index &hasher_plugin_t::identity() const noexcept { return class_identity; }

void hasher_plugin_t::activate(r::actor_base_t *actor) noexcept {
    r::plugin::plugin_base_t::activate(actor);
    reaction_on(reaction_t::INIT);
}

bool hasher_plugin_t::handle_init(r::message::init_request_t *message) noexcept {
    if (hashers.empty()) {
        actor->configure(*this);
    }
    auto r = r::plugin::plugin_base_t::handle_init(message);
    if (r) {
        for (auto &addr : hashers) {
            if (!addr) {
                return false;
            }
        }
    }
    reaction_off(reaction_t::INIT);
    return true;
}

void hasher_plugin_t::configure(std::uint32_t number_of_hashers) noexcept {
    hashers.resize(number_of_hashers);
    usages.resize(number_of_hashers);

    auto plugin = actor->access<to::get_plugin>(&r::plugin::registry_plugin_t::class_identity);
    auto registry = static_cast<r::plugin::registry_plugin_t *>(plugin);
    for (size_t i = 0; i < number_of_hashers; ++i) {
        auto name = fmt::format("hasher-{}", i + 1);
        registry->discover_name(name, hashers[i], true).link(false);
    }
}

void hasher_plugin_t::calc_digest(utils::bytes_t data, std::int32_t block_index) noexcept {
    static constexpr auto LIMIT = std::numeric_limits<int>::max();
    int next_dec = LIMIT;
    int zero_index = -1;
    for (size_t i = 0; i < hashers.size(); ++i) {
        auto &val = usages[i];
        if (val) {
            val -= min_usage;
        }
        if (val == 0) {
            if (zero_index < 0) {
                zero_index = i;
            }
        } else {
            if (val < next_dec) {
                next_dec = val;
            }
        }
    }
    assert(zero_index >= 0);
    if (next_dec != LIMIT) {
        min_usage = next_dec;
    }
    usages[zero_index] += static_cast<std::int32_t>(data.size());
    auto &addr = hashers[zero_index];
    actor->route<payload::digest_t>(addr, actor->get_address(), std::move(data), block_index);
}
