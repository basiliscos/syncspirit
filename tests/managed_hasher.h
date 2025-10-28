// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include "hasher/hasher_actor.h"
#include "hasher/messages.h"
#include "model/misc/arc.hpp"

#include "syncspirit-test-export.h"

namespace syncspirit::test {

namespace r = rotor;

struct managed_hasher_config_t : hasher::hasher_actor_config_t {
    uint32_t index;
    bool auto_reply = true;
};

template <typename Actor> struct hasher_config_builder_t : hasher::hasher_actor_config_builder_t<Actor> {
    using builder_t = typename Actor::template config_builder_t<Actor>;
    using parent_t = hasher::hasher_actor_config_builder_t<Actor>;
    using parent_t::parent_t;

    builder_t &&auto_reply(bool value = true) && noexcept {
        parent_t::config.auto_reply = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
};

struct SYNCSPIRIT_TEST_API managed_hasher_t : r::actor_base_t {
    using config_t = managed_hasher_config_t;
    template <typename Actor> using config_builder_t = hasher_config_builder_t<Actor>;

    using digest_request_t = hasher::message::digest_t;
    using digest_request_ptr_t = model::intrusive_ptr_t<digest_request_t>;
    using digest_queue_t = std::deque<digest_request_ptr_t>;

    managed_hasher_t(config_t &cfg);

    void configure(r::plugin::plugin_base_t &plugin) noexcept override;
    void on_digest(digest_request_t &req) noexcept;
    void process_requests() noexcept;

    uint32_t index;
    std::uint64_t digested_bytes = 0;
    bool auto_reply;
    utils::logger_t log;
    digest_queue_t digest_queue;
};

} // namespace syncspirit::test
