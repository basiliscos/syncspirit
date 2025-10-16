// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include "utils/log.h"
#include "messages.h"
#include "syncspirit-export.h"

#include <rotor.hpp>

namespace syncspirit {
namespace hasher {

struct hasher_actor_config_t : r::actor_config_t {
    uint32_t index;
};

template <typename Actor> struct hasher_actor_config_builder_t : r::actor_config_builder_t<Actor> {
    using builder_t = typename Actor::template config_builder_t<Actor>;
    using parent_t = r::actor_config_builder_t<Actor>;
    using parent_t::parent_t;

    builder_t &&index(uint32_t value) && noexcept {
        parent_t::config.index = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
};

struct SYNCSPIRIT_API hasher_actor_t : public r::actor_base_t {
    using config_t = hasher_actor_config_t;
    template <typename Actor> using config_builder_t = hasher_actor_config_builder_t<Actor>;

    explicit hasher_actor_t(config_t &cfg);

    void configure(r::plugin::plugin_base_t &plugin) noexcept override;
    void on_start() noexcept override;
    void shutdown_finish() noexcept override;

  private:
    void on_digest(message::digest_t &req) noexcept;
    void on_validation(message::validation_t &req) noexcept;

    utils::logger_t log;
    uint32_t index;
};

} // namespace hasher
} // namespace syncspirit
