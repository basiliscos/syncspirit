#pragma once

#include "../config/bep.h"
#include "../model/cluster.h"
#include "../ui/messages.hpp"
#include "messages.h"
#include <boost/asio.hpp>
#include <rotor/asio.hpp>
#include <map>

namespace syncspirit {
namespace net {

struct cluster_supervisor_config_t : ra::supervisor_config_asio_t {
    model::device_ptr_t device;
    model::cluster_ptr_t cluster;
    model::devices_map_t *devices;
    config::main_t::folders_t *folders;
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

    builder_t &&folders(config::main_t::folders_t *value) &&noexcept {
        parent_t::config.folders = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
};

struct cluster_supervisor_t : public ra::supervisor_asio_t {
    using parent_t = ra::supervisor_asio_t;
    using config_t = cluster_supervisor_config_t;
    template <typename Actor> using config_builder_t = cluster_supervisor_config_builder_t<Actor>;

    explicit cluster_supervisor_t(cluster_supervisor_config_t &config);
    void configure(r::plugin::plugin_base_t &plugin) noexcept override;
    // void on_child_shutdown(actor_base_t *actor) noexcept override;
    void on_start() noexcept override;

  private:
    using folder_iterator_t = typename config::main_t::folders_t::iterator;
    using create_folder_req_ptr_t = r::intrusive_ptr_t<ui::message::create_folder_request_t>;
    using folder_requests_t = std::unordered_map<r::request_id_t, create_folder_req_ptr_t>;

    void on_create_folder(ui::message::create_folder_request_t &message) noexcept;

    void load_db() noexcept;
    void load_cluster(folder_iterator_t it) noexcept;
    void on_load_folder(message::load_folder_response_t &message) noexcept;
    void on_make_index(message::make_index_id_response_t &message) noexcept;

    r::address_ptr_t coordinator;
    r::address_ptr_t db;
    model::device_ptr_t device;
    model::cluster_ptr_t cluster;
    model::devices_map_t *devices;
    config::main_t::folders_t *folders;
    folder_requests_t folder_requests;
};

} // namespace net
} // namespace syncspirit
