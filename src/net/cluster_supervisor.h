#pragma once

#include "../config/bep.h"
#include "../model/cluster.h"
#include "../model/folder.h"
#include "../fs/messages.h"
#include "../ui/messages.hpp"
#include <boost/asio.hpp>
#include <rotor/asio.hpp>

namespace syncspirit {
namespace net {

namespace bfs = boost::filesystem;

struct cluster_supervisor_config_t : ra::supervisor_config_asio_t {
    config::bep_config_t bep_config;
    model::device_ptr_t device;
    model::cluster_ptr_t cluster;
    model::devices_map_t *devices;
    model::ignored_folders_map_t *ignored_folders;
};

template <typename Supervisor>
struct cluster_supervisor_config_builder_t : ra::supervisor_config_asio_builder_t<Supervisor> {
    using builder_t = typename Supervisor::template config_builder_t<Supervisor>;
    using parent_t = ra::supervisor_config_asio_builder_t<Supervisor>;
    using parent_t::parent_t;

    builder_t &&device(const model::device_ptr_t &value) &&noexcept {
        parent_t::config.device = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&bep_config(const config::bep_config_t &value) &&noexcept {
        parent_t::config.bep_config = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&cluster(const model::cluster_ptr_t &value) &&noexcept {
        parent_t::config.cluster = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&devices(model::devices_map_t *value) &&noexcept {
        parent_t::config.devices = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&ignored_folders(model::ignored_folders_map_t *value) &&noexcept {
        parent_t::config.ignored_folders = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
};

struct cluster_supervisor_t : public ra::supervisor_asio_t {
    using parent_t = ra::supervisor_asio_t;
    using config_t = cluster_supervisor_config_t;
    template <typename Actor> using config_builder_t = cluster_supervisor_config_builder_t<Actor>;

    explicit cluster_supervisor_t(cluster_supervisor_config_t &config);
    void configure(r::plugin::plugin_base_t &plugin) noexcept override;
    void on_child_shutdown(actor_base_t *actor) noexcept override;
    void on_start() noexcept override;
    void shutdown_start() noexcept override;

  private:
    struct scan_info_t {
        model::folder_ptr_t folder;
        r::request_id_t request_id;
    };

    using device2addr_map_t = std::unordered_map<std::string, r::address_ptr_t>; // device_id: controller
    using addr2device_map_t = std::unordered_map<r::address_ptr_t, std::string>; // reverse
    using create_folder_req_t = r::intrusive_ptr_t<ui::message::create_folder_request_t>;
    using scan_folders_map_t = std::unordered_map<bfs::path, scan_info_t>;

    void on_create_folder(ui::message::create_folder_request_t &message) noexcept;
    void on_connect(message::connect_notify_t &message) noexcept;
    void on_disconnect(message::disconnect_notify_t &message) noexcept;
    void on_store_new_folder(message::store_new_folder_response_t &message) noexcept;
    void on_scan_complete(fs::message::scan_response_t &message) noexcept;
    void on_scan_error(fs::message::scan_error_t &message) noexcept;
    void scan(const model::folder_ptr_t &folder, void *scan_handler) noexcept;
    void handle_scan_initial(model::folder_ptr_t &folder) noexcept;
    void handle_scan_new(model::folder_ptr_t &folder) noexcept;

    r::address_ptr_t coordinator;
    r::address_ptr_t fs;
    r::address_ptr_t db;
    config::bep_config_t bep_config;
    model::device_ptr_t device;
    model::cluster_ptr_t cluster;
    model::devices_map_t *devices;
    model::folders_map_t &folders;
    model::ignored_folders_map_t *ignored_folders;
    device2addr_map_t device2addr_map;
    addr2device_map_t addr2device_map;
    create_folder_req_t create_folder_req;
    scan_folders_map_t scan_folders_map;
};

} // namespace net
} // namespace syncspirit
