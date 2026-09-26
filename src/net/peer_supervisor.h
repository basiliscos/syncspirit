// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2026 Ivan Baidakou

#pragma once

#include "config/bep.h"
#include "config/relay.h"
#include "messages.h"
#include "model/messages.h"
#include "model_actor.hpp"
#include "model/diff/cluster_visitor.h"
#include <boost/asio.hpp>
#include <rotor/asio.hpp>

namespace syncspirit {
namespace net {

namespace payload {
struct peer_connected_t;
}

namespace message {
using peer_connected_t = r::message_t<payload::peer_connected_t>;
}

namespace outcome = boost::outcome_v2;

struct SYNCSPIRIT_API peer_supervisor_t final : public model_actor_t<ra::supervisor_asio_t>,
                                                private model::diff::cluster_visitor_t {
    using parent_t = model_actor_t<ra::supervisor_asio_t>;

    struct config_t : parent_t::config_t {
        using base_t = model_actor_t<ra::supervisor_asio_t>::config_t;
        using base_t::base_t;
        std::string_view device_name;
        const utils::key_pair_t *ssl_pair;
        config::bep_config_t bep_config;
        config::relay_config_t relay_config;
    };

    template <typename Actor> struct config_builder_t : parent_t::template config_builder_t<Actor> {
        using builder_t = typename Actor::template config_builder_t<Actor>;
        using base_t = parent_t::template config_builder_t<Actor>;
        using base_t::base_t;

        builder_t &&device_name(std::string_view value) && noexcept {
            base_t::config.device_name = value;
            return std::move(*static_cast<typename base_t::builder_t *>(this));
        }

        builder_t &&ssl_pair(const utils::key_pair_t *value) && noexcept {
            base_t::config.ssl_pair = value;
            return std::move(*static_cast<typename base_t::builder_t *>(this));
        }

        builder_t &&bep_config(const config::bep_config_t &value) && noexcept {
            base_t::config.bep_config = value;
            return std::move(*static_cast<typename base_t::builder_t *>(this));
        }

        builder_t &&relay_config(const config::relay_config_t &value) && noexcept {
            base_t::config.relay_config = value;
            return std::move(*static_cast<typename base_t::builder_t *>(this));
        }
    };

    explicit peer_supervisor_t(config_t &config);
    void configure(r::plugin::plugin_base_t &plugin) noexcept override;
    void post_configure_coordinator() noexcept override;
    void on_child_shutdown(actor_base_t *actor) noexcept override;
    void on_start() noexcept override;
    void visit(const model::diff::cluster_diff_t &, model::payload::apply_context_t &) noexcept override;

  private:
    void on_connect(message::connect_request_t &) noexcept;
    void on_peer_ready(message::peer_connected_t &) noexcept;
    void on_connected(message::peer_connected_t &) noexcept;

    outcome::result<void> operator()(const model::diff::contact::dial_request_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::contact::connect_request_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::contact::relay_connect_request_t &, void *) noexcept override;

    r::address_ptr_t addr_unknown;
    std::string_view device_name;
    const utils::key_pair_t &ssl_pair;
    config::bep_config_t bep_config;
    config::relay_config_t relay_config;
};

} // namespace net
} // namespace syncspirit
