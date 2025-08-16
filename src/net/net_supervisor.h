// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include "model/messages.h"
#include "model/cluster.h"
#include "model/misc/sequencer.h"
#include "model/diff/iterative_controller.h"
#include "model/diff/cluster_visitor.h"
#include "config/main.h"
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
    size_t independent_threads = 0;
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

    builder_t &&independent_threads(size_t value) && noexcept {
        parent_t::config.independent_threads = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&sequencer(model::sequencer_ptr_t value) && noexcept {
        parent_t::config.sequencer = std::move(value);
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
};

template <typename T> using net_supervisor_base_t = model::diff::iterative_controller_t<T, ra::supervisor_asio_t, false>;

struct SYNCSPIRIT_API net_supervisor_t : net_supervisor_base_t<ra::supervisor_asio_t> {
    using parent_t = net_supervisor_base_t<ra::supervisor_asio_t>;
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
    void on_model_request(model::message::model_request_t &message) noexcept;
    void on_thread_ready(model::message::thread_ready_t &) noexcept;
    void on_app_ready(model::message::app_ready_t &) noexcept;

    void dial_peer(const model::device_id_t &peer_device_id, const utils::uri_container_t &uris) noexcept;
    void launch_early() noexcept;
    void launch_cluster() noexcept;
    void load_db() noexcept;
    void seed_model() noexcept;

    void commit_loading() noexcept override;
    outcome::result<void> save_config(const config::main_t &new_cfg) noexcept;

    model::sequencer_ptr_t sequencer;
    config::main_t app_config;
    size_t independent_threads;
    size_t cluster_copies;
    model::diff::cluster_diff_ptr_t load_diff;
    r::address_ptr_t db_addr;
    utils::key_pair_t ssl_pair;
};

} // namespace net
} // namespace syncspirit
