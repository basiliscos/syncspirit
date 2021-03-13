#pragma once

#include "../config/dialer.h"
#include "messages.h"
#include <optional>
#include <unordered_map>

namespace syncspirit {
namespace net {

struct dialer_actor_config_t : r::actor_config_t {
    config::dialer_config_t dialer_config;
    model::device_ptr_t device;
    model::devices_map_t *devices;
};

template <typename Actor> struct dialer_actor_config_builder_t : r::actor_config_builder_t<Actor> {
    using builder_t = typename Actor::template config_builder_t<Actor>;
    using parent_t = r::actor_config_builder_t<Actor>;
    using parent_t::parent_t;

    builder_t &&dialer_config(const config::dialer_config_t &value) &&noexcept {
        parent_t::config.dialer_config = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&device(const model::device_ptr_t &value) &&noexcept {
        parent_t::config.device = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&devices(model::devices_map_t *value) &&noexcept {
        parent_t::config.devices = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
};

struct dialer_actor_t : public r::actor_base_t {
    using config_t = dialer_actor_config_t;
    template <typename Actor> using config_builder_t = dialer_actor_config_builder_t<Actor>;

    explicit dialer_actor_t(config_t &config);
    void configure(r::plugin::plugin_base_t &plugin) noexcept override;
    void shutdown_start() noexcept override;
    void on_start() noexcept override;

  private:
    using redial_map_t = std::unordered_map<model::device_ptr_t, rotor::request_id_t>;
    using discovery_map_t = std::unordered_map<model::device_ptr_t, rotor::request_id_t>;
    using online_map_t = std::unordered_set<model::device_ptr_t>;

    void on_announce(message::announce_notification_t &message) noexcept;
    void on_connect(message::connect_notify_t &message) noexcept;
    void on_disconnect(message::disconnect_notify_t &message) noexcept;
    void on_discovery(message::discovery_response_t &req) noexcept;
    void on_add(message::add_device_t &message) noexcept;
    void on_remove(message::remove_device_t &message) noexcept;
    void on_update(message::update_device_t &message) noexcept;

    void discover(const model::device_ptr_t &device) noexcept;
    void remove(const model::device_ptr_t &device) noexcept;
    void on_ready(const model::device_ptr_t &device, const utils::uri_container_t &uris) noexcept;
    void schedule_redial(const model::device_ptr_t &device) noexcept;
    void on_timer(r::request_id_t request_id, bool cancelled) noexcept;

    r::address_ptr_t coordinator;
    r::address_ptr_t cluster;
    r::address_ptr_t global_discovery;

    pt::time_duration redial_timeout;
    model::device_ptr_t device;
    model::devices_map_t *devices;
    discovery_map_t discovery_map;
    online_map_t online_map;
    redial_map_t redial_map;
};

} // namespace net
} // namespace syncspirit
