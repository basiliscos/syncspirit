// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025-2026 Ivan Baidakou

#pragma once

#include "utils/log.h"
#include "syncspirit-export.h"
#include "messages.hpp"

#include <rotor/request.hpp>

namespace syncspirit::bouncer {

namespace r = rotor;

struct SYNCSPIRIT_API bouncer_actor_t : r::actor_base_t {
    using parent_t = r::actor_base_t;
    using parent_t::parent_t;

    struct config_t : parent_t::config_t {
        bool flush_logs = false;
    };

    template <typename Actor> struct config_builder_t : parent_t::template config_builder_t<Actor> {
        using builder_t = typename Actor::template config_builder_t<Actor>;
        using base_t = parent_t::template config_builder_t<Actor>;
        using base_t::base_t;

        builder_t &&flush_logs(bool value) && noexcept {
            base_t::config.flush_logs = value;
            return std::move(*static_cast<typename base_t::builder_t *>(this));
        }
    };

    bouncer_actor_t(config_t &config);

    void configure(r::plugin::plugin_base_t &plugin) noexcept override;
    void shutdown_start() noexcept override;
    void shutdown_finish() noexcept override;

  private:
    void on_package(message::package_t &message) noexcept;

    utils::logger_t log;
    bool flush_logs = false;
};

} // namespace syncspirit::bouncer
