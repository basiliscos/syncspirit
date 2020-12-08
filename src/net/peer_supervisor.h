#pragma once

#include "../configuration.h"
#include "messages.h"
#include <boost/asio.hpp>
#include <rotor/asio.hpp>
#include <list>

namespace syncspirit {
namespace net {

using peer_list_t = std::list<model::device_id_t>;

struct peer_supervisor_config_t : ra::supervisor_config_asio_t {
    std::string_view device_name;
    peer_list_t peer_list;
    const utils::key_pair_t *ssl_pair;
    config::bep_config_t bep_config;
};

template <typename Supervisor>
struct peer_supervisor_config_builder_t : ra::supervisor_config_asio_builder_t<Supervisor> {
    using builder_t = typename Supervisor::template config_builder_t<Supervisor>;
    using parent_t = ra::supervisor_config_asio_builder_t<Supervisor>;
    using parent_t::parent_t;

    builder_t &&peer_list(const peer_list_t &value) &&noexcept {
        parent_t::config.peer_list = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&device_name(const std::string_view &value) &&noexcept {
        parent_t::config.device_name = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&ssl_pair(const utils::key_pair_t *value) &&noexcept {
        parent_t::config.ssl_pair = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&bep_config(const config::bep_config_t &value) &&noexcept {
        parent_t::config.bep_config = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
};

struct peer_supervisor_t : public ra::supervisor_asio_t {
    using parent_t = ra::supervisor_asio_t;
    using config_t = peer_supervisor_config_t;
    template <typename Actor> using config_builder_t = peer_supervisor_config_builder_t<Actor>;

    explicit peer_supervisor_t(peer_supervisor_config_t &config);
    void configure(r::plugin::plugin_base_t &plugin) noexcept override;
    void on_child_shutdown(actor_base_t *actor, const std::error_code &ec) noexcept override;
    void on_start() noexcept override;

  private:
    void on_announce(message::announce_notification_t &msg) noexcept;
    void on_discovery(message::discovery_response_t &res) noexcept;
    void on_discovery_notify(message::discovery_notify_t &message) noexcept;
    void discover_next_peer() noexcept;

    void launch_peer(const model::device_id_t &peer_device, const model::peer_contact_t &contact) noexcept;

    r::address_ptr_t coordinator;
    peer_list_t peer_list;
    std::string_view device_name;
    const utils::key_pair_t &ssl_pair;
    peer_list_t discover_queue;
    config::bep_config_t bep_config;
};

} // namespace net
} // namespace syncspirit
