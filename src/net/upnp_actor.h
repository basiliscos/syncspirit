#pragma once

#include "messages.h"
#include "utils/log.h"
#include <boost/asio.hpp>
#include <rotor/asio/supervisor_asio.h>
#include <optional>

namespace syncspirit {
namespace net {

struct upnp_actor_config_t : r::actor_config_t {
    utils::URI descr_url;
    std::uint32_t rx_buff_size;
    std::uint16_t external_port;
    model::cluster_ptr_t cluster;
};

template <typename Actor> struct upnp_actor_config_builder_t : r::actor_config_builder_t<Actor> {
    using builder_t = typename Actor::template config_builder_t<Actor>;
    using parent_t = r::actor_config_builder_t<Actor>;
    using parent_t::parent_t;

    builder_t &&descr_url(const utils::URI &value) &&noexcept {
        parent_t::config.descr_url = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&external_port(std::uint16_t value) &&noexcept {
        parent_t::config.external_port = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&rx_buff_size(std::uint32_t value) &&noexcept {
        parent_t::config.rx_buff_size = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&cluster(const model::cluster_ptr_t &value) &&noexcept {
        parent_t::config.cluster = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
};

struct upnp_actor_t : public r::actor_base_t {
    using config_t = upnp_actor_config_t;
    template <typename Actor> using config_builder_t = upnp_actor_config_builder_t<Actor>;

    explicit upnp_actor_t(config_t &cfg);

    void on_start() noexcept override;
    void configure(r::plugin::plugin_base_t &plugin) noexcept override;
    void shutdown_start() noexcept override;

  private:
    using rx_buff_t = payload::http_request_t::rx_buff_ptr_t;
    using unlink_request_t = r::intrusive_ptr_t<r::message::unlink_request_t>;
    using request_option_t = std::optional<r::request_id_t>;

    void on_igd_description(message::http_response_t &res) noexcept;
    void on_external_ip(message::http_response_t &res) noexcept;
    void on_mapping_port(message::http_response_t &res) noexcept;
    void on_unmapping_port(message::http_response_t &res) noexcept;
    void make_request(const r::address_ptr_t &addr, utils::URI &uri, fmt::memory_buffer &&tx_buff,
                      bool get_local_address = false) noexcept;
    void request_finish() noexcept;

    model::cluster_ptr_t cluster;
    utils::logger_t log;
    asio::ip::address local_address;
    utils::URI main_url;
    utils::URI igd_control_url;
    r::address_ptr_t http_client;
    r::address_ptr_t coordinator;
    r::address_ptr_t addr_description; /* for routing */
    r::address_ptr_t addr_external_ip; /* for routing */
    r::address_ptr_t addr_mapping;     /* for routing */
    r::address_ptr_t addr_unmapping;   /* for routing */
    rx_buff_t rx_buff;
    std::uint32_t rx_buff_size;
    std::uint16_t external_port;
    std::uint16_t local_port;
    asio::ip::address external_addr;
    unlink_request_t unlink_request;
    request_option_t http_request;
};

} // namespace net
} // namespace syncspirit
