// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include "config/main.h"
#include "model/cluster.h"
#include "model/folder.h"
#include "model/messages.h"
#include "model/diff/cluster_visitor.h"
#include "model/misc/sequencer.h"
#include "utils/log.h"
#include <boost/asio.hpp>
#include <rotor/asio.hpp>

namespace syncspirit {
namespace net {

namespace r = rotor;
namespace ra = r::asio;
namespace bfs = std::filesystem;
namespace outcome = boost::outcome_v2;

struct cluster_supervisor_config_t : ra::supervisor_config_asio_t {
    config::main_t config;
    model::cluster_ptr_t cluster;
    model::sequencer_ptr_t sequencer;
};

template <typename Supervisor>
struct cluster_supervisor_config_builder_t : ra::supervisor_config_asio_builder_t<Supervisor> {
    using builder_t = typename Supervisor::template config_builder_t<Supervisor>;
    using parent_t = ra::supervisor_config_asio_builder_t<Supervisor>;
    using parent_t::parent_t;

    builder_t &&config(const config::main_t &value) && noexcept {
        parent_t::config.config = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&cluster(const model::cluster_ptr_t &value) && noexcept {
        parent_t::config.cluster = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&sequencer(model::sequencer_ptr_t value) && noexcept {
        parent_t::config.sequencer = std::move(value);
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
};

struct SYNCSPIRIT_API cluster_supervisor_t : public ra::supervisor_asio_t, private model::diff::cluster_visitor_t {
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

    outcome::result<void> operator()(const model::diff::contact::peer_state_t &, void *) noexcept override;

    utils::logger_t log;
    r::address_ptr_t coordinator;
    config::main_t config;
    model::cluster_ptr_t cluster;
    model::sequencer_ptr_t sequencer;
};

} // namespace net
} // namespace syncspirit
