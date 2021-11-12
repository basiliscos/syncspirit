#pragma once

#include "../model/device.h"
#include "../model/cluster.h"
#include "../model/diff/diff_visitor.h"
#include "../ui/messages.hpp"
#include "../utils/log.h"
#include "messages.h"
#include <boost/asio.hpp>
#include <rotor/asio.hpp>
#include <unordered_map>
#include <boost/outcome.hpp>

namespace syncspirit {
namespace net {

namespace outcome = boost::outcome_v2;

struct net_supervisor_config_t : ra::supervisor_config_asio_t {
    config::main_t app_config;
    size_t cluster_copies = 0;
};

template <typename Supervisor>
struct net_supervisor_config_builder_t : ra::supervisor_config_asio_builder_t<Supervisor> {
    using builder_t = typename Supervisor::template config_builder_t<Supervisor>;
    using parent_t = ra::supervisor_config_asio_builder_t<Supervisor>;
    using parent_t::parent_t;

    builder_t &&app_config(const config::main_t &value) &&noexcept {
        parent_t::config.app_config = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&cluster_copies(size_t value) &&noexcept {
        parent_t::config.cluster_copies = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
};

struct net_supervisor_t : public ra::supervisor_asio_t, private model::diff::diff_visitor_t {
    using parent_t = ra::supervisor_asio_t;
    using config_t = net_supervisor_config_t;

    template <typename Actor> using config_builder_t = net_supervisor_config_builder_t<Actor>;

    explicit net_supervisor_t(config_t &config);
    void configure(r::plugin::plugin_base_t &plugin) noexcept override;
    void on_child_init(actor_base_t *actor, const r::extended_error_ptr_t &ec) noexcept override;
    void on_child_shutdown(actor_base_t *actor) noexcept override;
    void on_start() noexcept override;

  private:

    void on_ssdp(message::ssdp_notification_t &message) noexcept;
    void on_port_mapping(message::port_mapping_notification_t &message) noexcept;
    void on_discovery_notify(message::discovery_notify_t &message) noexcept;
    void on_connect(message::connect_response_t &message) noexcept;
/*
    void on_connection(message::connection_notify_t &message) noexcept;
    void on_dial_ready(message::dial_ready_notify_t &message) noexcept;
*/
    void on_config_request(ui::message::config_request_t &message) noexcept;
    void on_config_save(ui::message::config_save_request_t &message) noexcept;
    void on_load_cluster(message::load_cluster_response_t &message) noexcept;
    void on_model_update(message::model_update_t &message) noexcept;
    void on_model_request(message::model_request_t &message) noexcept;

    void dial_peer(const model::device_id_t &peer_device_id, const utils::uri_container_t &uris) noexcept;
    void launch_early() noexcept;
    void launch_cluster() noexcept;
    void launch_ssdp() noexcept;
    void launch_net() noexcept;
    void launch_upnp() noexcept;
    void load_db() noexcept;
    void seed_model() noexcept;

    outcome::result<void> save_config(const config::main_t &new_cfg) noexcept;
    outcome::result<void> operator()(const model::diff::load::load_cluster_t &) noexcept override;


    utils::logger_t log;
    config::main_t app_config;
    size_t seed;
    size_t cluster_copies;
    model::diff::cluster_diff_ptr_t load_diff;
    r::address_ptr_t db_addr;
    r::address_ptr_t ssdp_addr;
    r::address_ptr_t upnp_addr;
    r::address_ptr_t peers_addr;
    r::address_ptr_t cluster_addr;
    r::address_ptr_t controller_addr;
    r::address_ptr_t local_discovery_addr;
    std::uint32_t ssdp_attempts = 0;
    model::cluster_ptr_t cluster;
    utils::key_pair_t ssl_pair;
    utils::URI igd_location;
};

} // namespace net
} // namespace syncspirit
