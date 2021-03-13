#pragma once

#include "../config/bep.h"
#include "../model/cluster.h"
#include "../model/folder.h"
#include "../ui/messages.hpp"
#include "messages.h"
#include <boost/asio.hpp>
#include <rotor/asio.hpp>

namespace syncspirit {
namespace net {

struct cluster_supervisor_config_t : ra::supervisor_config_asio_t {
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

  private:
    using actors_map_t = std::unordered_map<std::string, r::address_ptr_t>;     // folder_id: folder_actor
    using syncing_map_t = std::unordered_map<std::string, model::folder_ptr_t>; // device_id: folder

    void on_create_folder(ui::message::create_folder_request_t &message) noexcept;
    void on_connect(message::connect_notify_t &message) noexcept;
    void on_disconnect(message::disconnect_notify_t &message) noexcept;

    //    void on_make_index(message::make_index_id_response_t &message) noexcept;

    r::address_ptr_t coordinator;
    r::address_ptr_t db;
    model::device_ptr_t device;
    model::cluster_ptr_t cluster;
    model::devices_map_t *devices;
    model::folders_map_t folders;
    model::ignored_folders_map_t *ignored_folders;
    actors_map_t actors_map;
    syncing_map_t syncing_map;
};

} // namespace net
} // namespace syncspirit
