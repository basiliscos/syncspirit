#pragma once

#include <rotor.hpp>
#include "command.h"
#include "model/messages.h"
#include "model/diff/cluster_visitor.h"
#include "utils/log.h"

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

struct governor_actor_t : public r::actor_base_t, private model::diff::cluster_visitor_t {
    using config_t = governor_actor_config_t;
    template <typename Actor> using config_builder_t = governor_actor_config_builder_t<Actor>;

    explicit governor_actor_t(config_t &cfg);

    void configure(r::plugin::plugin_base_t &plugin) noexcept override;
    void on_start() noexcept override;
    void shutdown_start() noexcept override;

    r::address_ptr_t coordinator;
    model::cluster_ptr_t cluster;
    Commands commands;
    utils::logger_t log;

  private:
    void process() noexcept;
    void on_model_update(model::message::forwarded_model_update_t &message) noexcept;
    void on_model_response(model::message::model_response_t& res) noexcept;
    void on_io_error(model::message::io_error_t& reply) noexcept;
};

} // namespace syncspirit::daemon
