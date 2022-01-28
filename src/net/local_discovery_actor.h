#pragma once

#include "messages.h"
#include "utils/log.h"
#include <boost/asio.hpp>
#include <optional>

namespace syncspirit {
namespace net {

namespace outcome = boost::outcome_v2;

struct local_discovery_actor_config_t : r::actor_config_t {
    std::uint16_t port;
    std::uint32_t frequency;
    model::cluster_ptr_t cluster;
};

template <typename Actor> struct local_discovery_actor_config_builder_t : r::actor_config_builder_t<Actor> {
    using builder_t = typename Actor::template config_builder_t<Actor>;
    using parent_t = r::actor_config_builder_t<Actor>;
    using parent_t::parent_t;

    builder_t &&port(const std::uint16_t value) &&noexcept {
        parent_t::config.port = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&frequency(const std::uint32_t value) &&noexcept {
        parent_t::config.frequency = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&cluster(const model::cluster_ptr_t &value) &&noexcept {
        parent_t::config.cluster = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
};

struct local_discovery_actor_t final : public r::actor_base_t {
    using config_t = local_discovery_actor_config_t;
    template <typename Actor> using config_builder_t = local_discovery_actor_config_builder_t<Actor>;

    explicit local_discovery_actor_t(config_t &cfg);

    void on_start() noexcept override;
    void shutdown_start() noexcept override;
    void configure(r::plugin::plugin_base_t &plugin) noexcept override;

  private:
    void init() noexcept;
    void announce() noexcept;
    void do_read() noexcept;

    void on_read(size_t bytes) noexcept;
    void on_read_error(const sys::error_code &ec) noexcept;
    void on_write(size_t bytes) noexcept;
    void on_write_error(const sys::error_code &ec) noexcept;
    void on_timer(r::request_id_t, bool cancelled) noexcept;

    utils::logger_t log;
    r::pt::time_duration frequency;
    asio::io_context::strand &strand;
    udp_socket_t sock;
    r::address_ptr_t coordinator;
    fmt::memory_buffer rx_buff;
    fmt::memory_buffer tx_buff;
    udp::endpoint bc_endpoint;
    udp::endpoint peer_endpoint;
    std::optional<r::request_id_t> timer_request;
    std::optional<r::request_id_t> endpoint_request;
    model::cluster_ptr_t cluster;
};

} // namespace net
} // namespace syncspirit
