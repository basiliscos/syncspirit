#pragma once

#include <rotor.hpp>
#include "command.h"
#include "../net/messages.h"
#include "../ui/messages.hpp"
#include "../utils/log.h"
#include "../model/cluster.h"

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

struct governor_actor_t : public r::actor_base_t {
    using config_t = governor_actor_config_t;
    template <typename Actor> using config_builder_t = governor_actor_config_builder_t<Actor>;

    explicit governor_actor_t(config_t &cfg);

    void configure(r::plugin::plugin_base_t &plugin) noexcept override;
    void on_start() noexcept override;
    void shutdown_start() noexcept override;

    void cmd_add_peer(const model::device_ptr_t &peer) noexcept;
    void cmd_add_folder(const db::Folder &folder) noexcept;
    void cmd_share_folder(const model::folder_ptr_t &folder, const model::device_ptr_t &device) noexcept;

    r::address_ptr_t coordinator;
    r::address_ptr_t cluster;
    Commands commands;
    model::cluster_ptr_t cluster_copy;
    model::devices_map_t devices_copy;
    utils::logger_t log;

  private:
    void process() noexcept;
    void on_cluster_ready(net::message::cluster_ready_notify_t &message) noexcept;
    void on_update_peer(ui::message::update_peer_response_t &message) noexcept;
    void on_folder_create(ui::message::create_folder_response_t &message) noexcept;
    void on_folder_share(ui::message::share_folder_response_t &message) noexcept;
};

} // namespace syncspirit::daemon
