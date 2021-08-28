#pragma once

#include "../config/bep.h"
#include "messages.h"
#include "../utils/log.h"
#include <boost/asio.hpp>
#include <rotor/asio.hpp>
#include <map>

namespace syncspirit {
namespace net {

struct peer_supervisor_config_t : ra::supervisor_config_asio_t {
    std::string_view device_name;
    const utils::key_pair_t *ssl_pair;
    config::bep_config_t bep_config;
};

template <typename Supervisor>
struct peer_supervisor_config_builder_t : ra::supervisor_config_asio_builder_t<Supervisor> {
    using builder_t = typename Supervisor::template config_builder_t<Supervisor>;
    using parent_t = ra::supervisor_config_asio_builder_t<Supervisor>;
    using parent_t::parent_t;

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
    void on_child_shutdown(actor_base_t *actor) noexcept override;
    void on_start() noexcept override;

  private:
    using connect_ptr_t = r::intrusive_ptr_t<message::connect_request_t>;
    using id2addr_t = std::map<model::device_id_t, r::address_ptr_t>;
    using addr2id_t = std::map<r::address_ptr_t, model::device_id_t>;
    using addr2req_t = std::map<r::address_ptr_t, connect_ptr_t>;

    void on_connect_request(message::connect_request_t &msg) noexcept;
    void on_connect_notify(message::connect_notify_t &msg) noexcept;

    utils::logger_t log;
    r::address_ptr_t coordinator;
    std::string_view device_name;
    const utils::key_pair_t &ssl_pair;
    config::bep_config_t bep_config;
    id2addr_t id2addr;
    addr2id_t addr2id;
    addr2req_t addr2req;
};

} // namespace net
} // namespace syncspirit
