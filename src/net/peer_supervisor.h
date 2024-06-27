// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include "config/bep.h"
#include "config/relay.h"
#include "messages.h"
#include "model/messages.h"
#include "model/diff/cluster_visitor.h"
#include "model/diff/contact_visitor.h"
#include "utils/log.h"
#include <boost/asio.hpp>
#include <rotor/asio.hpp>
#include <map>

namespace syncspirit {
namespace net {

namespace payload {
struct peer_connected_t;
}

namespace message {
using peer_connected_t = r::message_t<payload::peer_connected_t>;
}

namespace outcome = boost::outcome_v2;

struct peer_supervisor_config_t : ra::supervisor_config_asio_t {
    std::string_view device_name;
    const utils::key_pair_t *ssl_pair;
    config::bep_config_t bep_config;
    config::relay_config_t relay_config;
    model::cluster_ptr_t cluster;
};

template <typename Supervisor>
struct peer_supervisor_config_builder_t : ra::supervisor_config_asio_builder_t<Supervisor> {
    using builder_t = typename Supervisor::template config_builder_t<Supervisor>;
    using parent_t = ra::supervisor_config_asio_builder_t<Supervisor>;
    using parent_t::parent_t;

    builder_t &&device_name(std::string_view value) && noexcept {
        parent_t::config.device_name = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&ssl_pair(const utils::key_pair_t *value) && noexcept {
        parent_t::config.ssl_pair = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&bep_config(const config::bep_config_t &value) && noexcept {
        parent_t::config.bep_config = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&relay_config(const config::relay_config_t &value) && noexcept {
        parent_t::config.relay_config = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&cluster(const model::cluster_ptr_t &value) && noexcept {
        parent_t::config.cluster = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
};

struct SYNCSPIRIT_API peer_supervisor_t : public ra::supervisor_asio_t,
                                          private model::diff::cluster_visitor_t,
                                          private model::diff::contact_visitor_t {
    using parent_t = ra::supervisor_asio_t;
    using config_t = peer_supervisor_config_t;
    template <typename Actor> using config_builder_t = peer_supervisor_config_builder_t<Actor>;

    explicit peer_supervisor_t(peer_supervisor_config_t &config);
    void configure(r::plugin::plugin_base_t &plugin) noexcept override;
    void on_child_shutdown(actor_base_t *actor) noexcept override;
    void on_start() noexcept override;

  private:
    void on_connect(message::connect_request_t &) noexcept;
    void on_model_update(model::message::model_update_t &) noexcept;
    void on_contact_update(model::message::contact_update_t &) noexcept;
    void on_peer_ready(message::peer_connected_t &) noexcept;
    void on_connected(message::peer_connected_t &) noexcept;

    outcome::result<void> operator()(const model::diff::contact::dial_request_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::contact::connect_request_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::contact::relay_connect_request_t &, void *) noexcept override;

    model::cluster_ptr_t cluster;
    utils::logger_t log;
    r::address_ptr_t coordinator;
    r::address_ptr_t addr_unknown;
    std::string_view device_name;
    const utils::key_pair_t &ssl_pair;
    config::bep_config_t bep_config;
    config::relay_config_t relay_config;
};

} // namespace net
} // namespace syncspirit
