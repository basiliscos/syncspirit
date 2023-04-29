// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2023 Ivan Baidakou

#pragma once

#include <rotor.hpp>
#include "command.h"
#include "model/messages.h"
#include "model/diff/block_visitor.h"
#include "model/diff/cluster_visitor.h"
#include "fs/messages.h"
#include "utils/log.h"
#include <set>

namespace syncspirit::daemon {

namespace r = rotor;

struct governor_actor_config_t : r::actor_config_t {
    Commands commands;
};

template <typename Actor> struct governor_actor_config_builder_t : r::actor_config_builder_t<Actor> {
    using builder_t = typename Actor::template config_builder_t<Actor>;
    using parent_t = r::actor_config_builder_t<Actor>;
    using parent_t::parent_t;

    builder_t &&commands(Commands &&value) &&noexcept {
        parent_t::config.commands = std::move(value);
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
};

struct governor_actor_t : public r::actor_base_t,
                          private model::diff::cluster_visitor_t,
                          private model::diff::block_visitor_t {
    using config_t = governor_actor_config_t;
    template <typename Actor> using config_builder_t = governor_actor_config_builder_t<Actor>;

    explicit governor_actor_t(config_t &cfg);

    void configure(r::plugin::plugin_base_t &plugin) noexcept override;
    void on_start() noexcept override;
    void shutdown_start() noexcept override;
    void track_inactivity() noexcept;
    void schedule_rescan_dirs(const r::pt::time_duration &intreval) noexcept;

    r::address_ptr_t coordinator;
    model::cluster_ptr_t cluster;
    Commands commands;
    utils::logger_t log;
    std::uint32_t inactivity_seconds;

  private:
    using clock_t = r::pt::microsec_clock;

    void schedule_rescan_dirs() noexcept;
    void process() noexcept;
    void on_model_update(model::message::forwarded_model_update_t &message) noexcept;
    void on_block_update(model::message::forwarded_block_update_t &message) noexcept;
    void on_model_response(model::message::model_response_t &res) noexcept;
    void on_io_error(model::message::io_error_t &reply) noexcept;
    void on_inacitvity_timer(r::request_id_t, bool cancelled) noexcept;
    void on_rescan_timer(r::request_id_t, bool cancelled) noexcept;
    void on_scan_completed(fs::message::scan_completed_t &message) noexcept;

    void rescan_folders();
    void refresh_deadline() noexcept;

    outcome::result<void> operator()(const model::diff::modify::clone_file_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::peer::cluster_update_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::peer::update_folder_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::append_block_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::clone_block_t &, void *) noexcept override;

    r::pt::ptime deadline;
    r::pt::time_duration dirs_rescan_interval;
    model::folders_map_t scaning_folders;
};

} // namespace syncspirit::daemon
