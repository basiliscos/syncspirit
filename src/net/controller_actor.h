#pragma once

#include "../configuration.h"
#include "messages.h"
#include "../ui/messages.hpp"

namespace syncspirit {
namespace net {

struct controller_actor_config_t : r::actor_config_t {
    model::device_id_t device_id;
    const config::configuration_t *config;
};

template <typename Actor> struct controller_actor_config_builder_t : r::actor_config_builder_t<Actor> {
    using builder_t = typename Actor::template config_builder_t<Actor>;
    using parent_t = r::actor_config_builder_t<Actor>;
    using parent_t::parent_t;

    builder_t &&device_id(const model::device_id_t &value) &&noexcept {
        parent_t::config.device_id = std::move(value);
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&config(const config::configuration_t *value) &&noexcept {
        parent_t::config.config = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
};

struct controller_actor_t : public r::actor_base_t {
    using config_t = controller_actor_config_t;
    template <typename Actor> using config_builder_t = controller_actor_config_builder_t<Actor>;

    controller_actor_t(config_t &config);
    void configure(r::plugin::plugin_base_t &plugin) noexcept override;
    void on_start() noexcept override;

  private:
    void on_discovery_notify(message::discovery_notify_t &message) noexcept;
    void on_config_request(ui::message::config_request_t &message) noexcept;
    void on_config_save(ui::message::config_save_request_t &message) noexcept;

    config::configuration_t app_config;
    model::device_id_t device_id;
    r::address_ptr_t coordinator;
    r::address_ptr_t peers;
};

} // namespace net
} // namespace syncspirit
