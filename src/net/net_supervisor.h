// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include "model/messages.h"
#include "model/cluster.h"
#include "model/misc/sequencer.h"
#include "model/diff/apply_controller.h"
#include "model/diff/cluster_visitor.h"
#include "utils/log.h"
#include "messages.h"
#include <boost/asio.hpp>
#include <rotor/asio.hpp>
#include <boost/outcome.hpp>

namespace syncspirit {
namespace net {

namespace outcome = boost::outcome_v2;

struct net_supervisor_config_t : ra::supervisor_config_asio_t {
    config::main_t app_config;
    size_t cluster_copies = 0;
    model::sequencer_ptr_t sequencer;
};

template <typename Supervisor>
struct net_supervisor_config_builder_t : ra::supervisor_config_asio_builder_t<Supervisor> {
    using builder_t = typename Supervisor::template config_builder_t<Supervisor>;
    using parent_t = ra::supervisor_config_asio_builder_t<Supervisor>;
    using parent_t::parent_t;

    builder_t &&app_config(const config::main_t &value) && noexcept {
        parent_t::config.app_config = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&cluster_copies(size_t value) && noexcept {
        parent_t::config.cluster_copies = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&sequencer(model::sequencer_ptr_t value) && noexcept {
        parent_t::config.sequencer = std::move(value);
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
};

struct SYNCSPIRIT_API net_supervisor_t : public ra::supervisor_asio_t,
                                         private model::diff::cluster_visitor_t,
                                         private model::diff::apply_controller_t {
    using parent_t = ra::supervisor_asio_t;
    using config_t = net_supervisor_config_t;

    template <typename Actor> using config_builder_t = net_supervisor_config_builder_t<Actor>;

    explicit net_supervisor_t(config_t &config);
    void configure(r::plugin::plugin_base_t &plugin) noexcept override;
    void on_child_init(actor_base_t *actor, const r::extended_error_ptr_t &ec) noexcept override;
    void on_child_shutdown(actor_base_t *actor) noexcept override;
    void on_start() noexcept override;
    void shutdown_finish() noexcept override;

  private:
    void on_load_cluster(message::load_cluster_response_t &message) noexcept;
    void on_model_update(model::message::model_update_t &message) noexcept;
    void on_model_request(model::message::model_request_t &message) noexcept;

    void dial_peer(const model::device_id_t &peer_device_id, const utils::uri_container_t &uris) noexcept;
    void launch_early() noexcept;
    void launch_cluster() noexcept;
    void launch_net() noexcept;
    void load_db() noexcept;
    void seed_model() noexcept;

    outcome::result<void> save_config(const config::main_t &new_cfg) noexcept;
    outcome::result<void> operator()(const model::diff::load::load_cluster_t &, void *custom) noexcept override;

    model::sequencer_ptr_t sequencer;
    utils::logger_t log;
    config::main_t app_config;
    size_t cluster_copies;
    model::diff::cluster_diff_ptr_t load_diff;
    model::device_id_t global_device;
    r::address_ptr_t db_addr;
    model::cluster_ptr_t cluster;
    utils::key_pair_t ssl_pair;
};

} // namespace net
} // namespace syncspirit
