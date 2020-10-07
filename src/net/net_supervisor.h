#pragma once

#include "../configuration.h"
#include "ssl.h"
#include "messages.h"
#include <boost/asio.hpp>
#include <rotor/asio.hpp>

namespace syncspirit {
namespace net {

struct net_supervisor_config_t : ra::supervisor_config_asio_t {
    config::configuration_t app_config;
};

template <typename Supervisor>
struct net_supervisor_config_builder_t : ra::supervisor_config_asio_builder_t<Supervisor> {
    using builder_t = typename Supervisor::template config_builder_t<Supervisor>;
    using parent_t = ra::supervisor_config_asio_builder_t<Supervisor>;
    using parent_t::parent_t;

    builder_t &&app_config(const config::configuration_t &value) &&noexcept {
        parent_t::config.app_config = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
};

struct net_supervisor_t : public ra::supervisor_asio_t {
    using parent_t = ra::supervisor_asio_t;
    using config_t = net_supervisor_config_t;
    template <typename Actor> using config_builder_t = net_supervisor_config_builder_t<Actor>;

    explicit net_supervisor_t(config_t &config);
    void configure(r::plugin::plugin_base_t &plugin) noexcept override;
    void on_child_shutdown(actor_base_t *actor, const std::error_code &ec) noexcept override;

  private:
    void on_ssdp(message::ssdp_notification_t &message) noexcept;
    void on_announce(message::announce_notification_t &message) noexcept;
    void on_port_mapping(message::port_mapping_notification_t &message) noexcept;
    bool launch_ssdp() noexcept;

    config::configuration_t app_cfg;
    r::address_ptr_t ssdp_addr;
    std::uint32_t ssdp_attempts = 0;
    ssl_t ssl;
};

} // namespace net
} // namespace syncspirit
