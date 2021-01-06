#pragma once

#include "messages.h"
#include <boost/asio.hpp>
#include <optional>

namespace syncspirit {
namespace net {

struct global_discovery_actor_config_t : r::actor_config_t {
    tcp::endpoint endpoint;
    utils::URI announce_url;
    model::device_id_t device_id;
    const utils::key_pair_t *ssl_pair;
    std::uint32_t rx_buff_size;
    std::uint32_t io_timeout;
};

template <typename Actor> struct global_discovery_actor_config_builder_t : r::actor_config_builder_t<Actor> {
    using builder_t = typename Actor::template config_builder_t<Actor>;
    using parent_t = r::actor_config_builder_t<Actor>;
    using parent_t::parent_t;

    builder_t &&endpoint(const tcp::endpoint &value) &&noexcept {
        parent_t::config.endpoint = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&announce_url(const utils::URI &value) &&noexcept {
        parent_t::config.announce_url = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&ssl_pair(const utils::key_pair_t *value) &&noexcept {
        parent_t::config.ssl_pair = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&device_id(model::device_id_t &&value) &&noexcept {
        parent_t::config.device_id = std::move(value);
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&rx_buff_size(const std::uint32_t value) &&noexcept {
        parent_t::config.rx_buff_size = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&io_timeout(const std::uint32_t value) &&noexcept {
        parent_t::config.io_timeout = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
};

struct global_discovery_actor_t : public r::actor_base_t {
    using config_t = global_discovery_actor_config_t;
    template <typename Actor> using config_builder_t = global_discovery_actor_config_builder_t<Actor>;

    explicit global_discovery_actor_t(config_t &cfg);

    void on_start() noexcept override;
    void shutdown_start() noexcept override;
    void configure(r::plugin::plugin_base_t &plugin) noexcept override;

  private:
    using rx_buff_t = payload::http_request_t::rx_buff_ptr_t;
    using discovery_request_t = r::intrusive_ptr_t<message::discovery_request_t>;
    using discovery_queue_t = std::list<discovery_request_t>;

    void announce() noexcept;
    void on_announce_response(message::http_response_t &message) noexcept;
    void on_discovery_response(message::http_response_t &message) noexcept;
    void on_discovery(message::discovery_request_t &req) noexcept;
    void on_timer(r::request_id_t, bool cancelled) noexcept;
    void make_request(const r::address_ptr_t &addr, utils::URI &uri, fmt::memory_buffer &&tx_buff) noexcept;

    r::address_ptr_t http_client;
    r::address_ptr_t coordinator;
    tcp::endpoint endpoint;
    utils::URI announce_url;
    model::device_id_t dicovery_device_id;
    const utils::key_pair_t &ssl_pair;
    rx_buff_t rx_buff;
    std::uint32_t rx_buff_size;
    std::uint32_t io_timeout;
    bool announced = false;
    r::address_ptr_t addr_announce;  /* for routing */
    r::address_ptr_t addr_discovery; /* for routing */
    std::optional<r::request_id_t> timer_request;
    std::optional<r::request_id_t> http_request;
    discovery_queue_t discovery_queue;
};

} // namespace net
} // namespace syncspirit
