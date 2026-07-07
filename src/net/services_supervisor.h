// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#pragma once

#include "config/main.h"
#include "model/cluster.h"
#include "model/messages.h"
#include "model/misc/sequencer.h"
#include "utils/log.h"
#include <rotor/asio.hpp>
#include <cstdint>

namespace syncspirit::net {

namespace r = rotor;
namespace ra = r::asio;

struct SYNCSPIRIT_API services_supervisor_t final : ra::supervisor_asio_t {
    using parent_t = ra::supervisor_asio_t;
    struct config_t : parent_t::config_t {
        using base_t = parent_t::config_t;
        using base_t::base_t;
        config::main_t app_config;
        model::cluster_ptr_t cluster;
        model::sequencer_ptr_t sequencer;
        const utils::key_pair_t *ssl_pair = nullptr;
        bool *auto_restart = nullptr;
    };

    template <typename Actor> struct config_builder_t : parent_t::template config_builder_t<Actor> {
        using builder_t = typename Actor::template config_builder_t<Actor>;
        using base_t = parent_t::template config_builder_t<Actor>;
        using base_t::base_t;

        builder_t &&app_config(const config::main_t &value) && noexcept {
            base_t::config.app_config = value;
            return std::move(*static_cast<typename base_t::builder_t *>(this));
        }
        builder_t &&cluster(const model::cluster_ptr_t &value) && noexcept {
            base_t::config.cluster = value;
            return std::move(*static_cast<typename base_t::builder_t *>(this));
        }
        builder_t &&ssl_pair(const utils::key_pair_t *value) && noexcept {
            base_t::config.ssl_pair = value;
            return std::move(*static_cast<typename base_t::builder_t *>(this));
        }
        builder_t &&sequencer(model::sequencer_ptr_t value) && noexcept {
            base_t::config.sequencer = std::move(value);
            return std::move(*static_cast<typename base_t::builder_t *>(this));
        }
        builder_t &&auto_restart(bool &value) && noexcept {
            base_t::config.auto_restart = &value;
            return std::move(*static_cast<typename base_t::builder_t *>(this));
        }
    };

    explicit services_supervisor_t(config_t &config);
    void configure(r::plugin::plugin_base_t &plugin) noexcept override;
    void on_child_shutdown(actor_base_t *actor) noexcept override;
    void on_local_up(model::message::local_up_t &) noexcept;
    void on_start() noexcept override;
    void shutdown_start() noexcept override;
    bool should_restart() const noexcept override;

  private:
    void launch_acceptor() noexcept;
    void launch_cluster_supervisor() noexcept;
    void launch_dialer() noexcept;
    void launch_local_discovery() noexcept;
    void launch_global_discovery() noexcept;
    void launch_peer_supervisor() noexcept;
    void launch_relay() noexcept;
    void launch_resolver() noexcept;
    void launch_upnp() noexcept;
    void launch_http10() noexcept;

    utils::logger_t log;
    config::main_t app_config;
    r::address_ptr_t coordinator;
    model::cluster_ptr_t cluster;
    model::sequencer_ptr_t sequencer;
    std::uint32_t counter{0};
    const utils::key_pair_t &ssl_pair;
    bool *auto_restart;
};
} // namespace syncspirit::net
