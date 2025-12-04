// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "hasher_plugin.h"
#include "messages.h"
#include <fmt/format.h>
#include <limits>

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

bool hasher_plugin_t::handle_init(r::message::init_request_t *message) noexcept {
    return parent_t::handle_init(message) && [this]() -> bool {
        for (auto &addr : hashers) {
            if (!addr) {
                return false;
            }
        }
        return true;
    }();
}

void hasher_plugin_t::configure_hashers(std::uint32_t number) noexcept {
    hashers.resize(number);
    usages.resize(number);

    for (size_t i = 0; i < number; ++i) {
        auto name = fmt::format("hasher-{}", i + 1);
        discover_name(name, hashers[i], true).link(false);
    }
}

void hasher_plugin_t::calc_digest(utils::bytes_t data, std::int32_t block_index, const r::address_ptr_t &reply_back,
                                  payload::extendended_context_prt_t context) noexcept {
    static constexpr auto LIMIT = std::numeric_limits<int>::max();
    assert(hashers.size());
    int min_index;
    if (hashers.size() > 1) {
        min_index = -1;
        int min_value = LIMIT;
        for (size_t i = 0; i < hashers.size(); ++i) {
            auto &val = usages[i];
            val -= min_usage;
            if (val < min_value) {
                min_index = i;
                min_value = val;
            }
        }
        assert(min_index >= 0);
        min_usage = min_value;
        usages[min_index] += static_cast<std::int32_t>(data.size());
    } else {
        min_index = 0;
    }
    assert(min_index >= 0);
    auto &addr = hashers[min_index];
    actor->route<payload::digest_t>(addr, reply_back, std::move(data), block_index, std::move(context));
}
