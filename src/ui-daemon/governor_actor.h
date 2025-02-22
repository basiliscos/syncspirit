// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include <rotor.hpp>
#include "command.h"
#include "model/messages.h"
#include "model/diff/cluster_visitor.h"
#include "model/misc/sequencer.h"
#include "utils/log.h"

namespace syncspirit::daemon {

namespace r = rotor;

struct governor_actor_config_t : r::actor_config_t {
    Commands commands;
    model::sequencer_ptr_t sequencer;
    model::cluster_ptr_t cluster;
};

template <typename Actor> struct governor_actor_config_builder_t : r::actor_config_builder_t<Actor> {
    using builder_t = typename Actor::template config_builder_t<Actor>;
    using parent_t = r::actor_config_builder_t<Actor>;
    using parent_t::parent_t;

    builder_t &&commands(Commands &&value) && noexcept {
        parent_t::config.commands = std::move(value);
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

struct governor_actor_t : public r::actor_base_t, private model::diff::cluster_visitor_t {
    using config_t = governor_actor_config_t;
    template <typename Actor> using config_builder_t = governor_actor_config_builder_t<Actor>;

    explicit governor_actor_t(config_t &cfg);

    void configure(r::plugin::plugin_base_t &plugin) noexcept override;
    void on_start() noexcept override;
    void shutdown_start() noexcept override;
    void track_inactivity() noexcept;
    void process() noexcept;
    void send_command(model::diff::cluster_diff_ptr_t diff, command_t &source) noexcept;

    r::address_ptr_t coordinator;
    r::address_ptr_t fs_scanner;
    Commands commands;
    model::sequencer_ptr_t sequencer;
    model::cluster_ptr_t cluster;
    utils::logger_t log;
    std::uint32_t inactivity_seconds;

  private:
    using clock_t = r::pt::microsec_clock;

    void on_model_update(model::message::model_update_t &message) noexcept;
    void on_command(model::message::model_update_t &message) noexcept;
    void on_inactivity_timer(r::request_id_t, bool cancelled) noexcept;

    void refresh_deadline() noexcept;

    outcome::result<void> operator()(const model::diff::advance::remote_copy_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::local::io_failure_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::append_block_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::clone_block_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::peer::cluster_update_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::peer::update_folder_t &, void *) noexcept override;

    r::pt::ptime deadline;
};

} // namespace syncspirit::daemon
