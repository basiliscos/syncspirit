// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#pragma once

#include "config/bep.h"
#include "model/cluster.h"
#include "model/folder.h"
#include "model/messages.h"
#include "model/diff/cluster_visitor.h"
#include "utils/log.h"
#include <boost/asio.hpp>
#include <rotor/asio.hpp>

namespace syncspirit {
namespace net {

namespace r = rotor;
namespace ra = r::asio;
namespace bfs = boost::filesystem;
namespace outcome = boost::outcome_v2;

struct cluster_supervisor_config_t : ra::supervisor_config_asio_t {
    config::bep_config_t bep_config;
    std::uint32_t hasher_threads;
    model::cluster_ptr_t cluster;
};

template <typename Supervisor>
struct cluster_supervisor_config_builder_t : ra::supervisor_config_asio_builder_t<Supervisor> {
    using builder_t = typename Supervisor::template config_builder_t<Supervisor>;
    using parent_t = ra::supervisor_config_asio_builder_t<Supervisor>;
    using parent_t::parent_t;

    builder_t &&bep_config(const config::bep_config_t &value) &&noexcept {
        parent_t::config.bep_config = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&hasher_threads(std::uint32_t value) &&noexcept {
        parent_t::config.hasher_threads = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&cluster(const model::cluster_ptr_t &value) &&noexcept {
        parent_t::config.cluster = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
};

struct cluster_supervisor_t : public ra::supervisor_asio_t, private model::diff::cluster_visitor_t {
    using parent_t = ra::supervisor_asio_t;
    using config_t = cluster_supervisor_config_t;
    template <typename Actor> using config_builder_t = cluster_supervisor_config_builder_t<Actor>;

    explicit cluster_supervisor_t(cluster_supervisor_config_t &config);
    void configure(r::plugin::plugin_base_t &plugin) noexcept override;
    void on_child_shutdown(actor_base_t *actor) noexcept override;
    void on_start() noexcept override;
    void shutdown_start() noexcept override;

  private:
    void on_model_update(model::message::model_update_t &message) noexcept;

    outcome::result<void> operator()(const model::diff::peer::peer_state_t &) noexcept override;

    utils::logger_t log;
    r::address_ptr_t coordinator;
    config::bep_config_t bep_config;
    std::uint32_t hasher_threads;
    model::cluster_ptr_t cluster;
};

} // namespace net
} // namespace syncspirit
