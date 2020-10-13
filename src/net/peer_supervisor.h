#pragma once

#include "../configuration.h"
#include "messages.h"
#include <boost/asio.hpp>
#include <rotor/asio.hpp>
#include <list>

namespace syncspirit {
namespace net {

using peer_list_t = std::list<proto::device_id_t>;

struct peer_supervisor_config_t : ra::supervisor_config_asio_t {
    peer_list_t peer_list;
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
};

struct peer_supervisor_t : public ra::supervisor_asio_t {
    using parent_t = ra::supervisor_asio_t;
    using config_t = peer_supervisor_config_t;
    template <typename Actor> using config_builder_t = peer_supervisor_config_builder_t<Actor>;

    explicit peer_supervisor_t(peer_supervisor_config_t &config);
    void configure(r::plugin::plugin_base_t &plugin) noexcept override;

  private:
    void on_announce(message::announce_notification_t &msg) noexcept;
    void on_discovery(message::discovery_response_t &res) noexcept;
    void discover_next_peer() noexcept;

    r::address_ptr_t coordinator;
    peer_list_t peer_list;
    peer_list_t discover_queue;
};

} // namespace net
} // namespace syncspirit
