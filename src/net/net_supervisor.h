#pragma once

#include "../model/device.h"
#include "../model/cluster.h"
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
};

struct net_supervisor_t : public ra::supervisor_asio_t {
    using parent_t = ra::supervisor_asio_t;
    using config_t = net_supervisor_config_t;

    template <typename Actor> using config_builder_t = net_supervisor_config_builder_t<Actor>;

    explicit net_supervisor_t(config_t &config);
    void configure(r::plugin::plugin_base_t &plugin) noexcept override;
    void on_child_init(actor_base_t *actor, const r::extended_error_ptr_t &ec) noexcept override;
    void on_child_shutdown(actor_base_t *actor) noexcept override;
    void on_start() noexcept override;

  private:
    using ignore_device_req_t = r::intrusive_ptr_t<ui::message::ignore_device_request_t>;
    using ignore_folder_req_t = r::intrusive_ptr_t<ui::message::ignore_folder_request_t>;
    using update_peer_req_t = r::intrusive_ptr_t<ui::message::update_peer_request_t>;
    using ingored_folder_requests_t = std::list<ignore_folder_req_t>;

    void on_ssdp(message::ssdp_notification_t &message) noexcept;
    void on_port_mapping(message::port_mapping_notification_t &message) noexcept;
    void on_discovery_notify(message::discovery_notify_t &message) noexcept;
    void on_connect(message::connect_response_t &message) noexcept;
    void on_disconnect(message::disconnect_notify_t &message) noexcept;
    void on_connection(message::connection_notify_t &message) noexcept;
    void on_dial_ready(message::dial_ready_notify_t &message) noexcept;
    void on_auth(message::auth_request_t &message) noexcept;
    void on_config_request(ui::message::config_request_t &message) noexcept;
    void on_config_save(ui::message::config_save_request_t &message) noexcept;
    void on_load_cluster(message::load_cluster_response_t &message) noexcept;
    void on_cluster_ready(message::cluster_ready_notify_t &message) noexcept;
    void on_ignore_device(ui::message::ignore_device_request_t &message) noexcept;
    void on_update_peer(ui::message::update_peer_request_t &message) noexcept;
    void on_ignore_folder(ui::message::ignore_folder_request_t &message) noexcept;
    void on_store_ignored_device(message::store_ignored_device_response_t &message) noexcept;
    void on_store_device(message::store_device_response_t &message) noexcept;
    void on_store_ignored_folder(message::store_ignored_folder_response_t &message) noexcept;
    void on_store_new_folder(message::store_new_folder_response_t &message) noexcept;
    void on_create_folder(ui::message::create_folder_request_t &message) noexcept;

    void dial_peer(const model::device_id_t &peer_device_id, const utils::uri_container_t &uris) noexcept;
    void launch_early() noexcept;
    void launch_cluster() noexcept;
    void launch_ssdp() noexcept;
    void launch_net() noexcept;
    void launch_upnp() noexcept;
    void load_db() noexcept;
    outcome::result<void> save_config(const config::main_t &new_cfg) noexcept;

    utils::logger_t log;
    config::main_t app_config;
    r::address_ptr_t db_addr;
    r::address_ptr_t ssdp_addr;
    r::address_ptr_t upnp_addr;
    r::address_ptr_t peers_addr;
    r::address_ptr_t cluster_addr;
    r::address_ptr_t controller_addr;
    r::address_ptr_t local_discovery_addr;
    std::uint32_t ssdp_attempts = 0;
    model::device_ptr_t device;
    model::cluster_ptr_t cluster;
    utils::key_pair_t ssl_pair;
    utils::URI igd_location;
    model::devices_map_t devices;
    model::ignored_devices_map_t ignored_devices;
    model::ignored_folders_map_t ignored_folders;
    ignore_device_req_t ignore_device_req;
    ingored_folder_requests_t ingored_folder_requests;
    update_peer_req_t update_peer_req;
};

} // namespace net
} // namespace syncspirit
