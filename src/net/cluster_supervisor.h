#pragma once

#include "../config/bep.h"
#include "../model/cluster.h"
#include "../model/folder.h"
#include "../fs/messages.h"
#include "../utils/log.h"
#include "messages.h"
#include "model/diff/cluster_visitor.h"
#include <boost/asio.hpp>
#include <rotor/asio.hpp>

namespace syncspirit {
namespace net {

namespace bfs = boost::filesystem;
namespace outcome = boost::outcome_v2;

struct cluster_supervisor_config_t : ra::supervisor_config_asio_t {
    config::bep_config_t bep_config;
    std::uint32_t hasher_threads;
    model::cluster_ptr_t cluster;
};

template <typename Supervisor>
struct cluster_supervisor_config_builder_t : ra::supervisor_config_asio_builder_t<Supervisor> {
    using builder_t = typename Supervisor::template config_builder_t<Supervisor>;
    using parent_t = ra::supervisor_config_asio_builder_t<Supervisor>;
    using parent_t::parent_t;

    builder_t &&bep_config(const config::bep_config_t &value) &&noexcept {
        parent_t::config.bep_config = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&hasher_threads(std::uint32_t value) &&noexcept {
        parent_t::config.hasher_threads = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&cluster(const model::cluster_ptr_t &value) &&noexcept {
        parent_t::config.cluster = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

};

struct cluster_supervisor_t : public ra::supervisor_asio_t, private model::diff::cluster_visitor_t {
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
/*
    using addr2device_map_t = std::unordered_map<r::address_ptr_t, std::string>; // reverse
    using scan_folders_map_t = std::unordered_map<bfs::path, scan_info_t>;
    using scan_foders_it = typename scan_folders_map_t::iterator;
*/

/*
    void on_scan_complete_initial(fs::message::scan_response_t &message) noexcept;
    void on_scan_complete_new(fs::message::scan_response_t &message) noexcept;
    scan_foders_it on_scan_complete(fs::message::scan_response_t &message) noexcept;
    void on_scan_error(fs::message::scan_error_t &message) noexcept;
    void on_file_update(message::file_update_notify_t &message) noexcept;
    void scan(const model::folder_ptr_t &folder, r::address_ptr_t &via) noexcept;
*/
    void on_model_update(message::model_update_t &message) noexcept;

    outcome::result<void> operator()(const model::diff::peer::peer_state_t &) noexcept override;

    utils::logger_t log;
    r::address_ptr_t coordinator;
    /*
    r::address_ptr_t scan_addr;
    r::address_ptr_t scan_initial; // for routing
    r::address_ptr_t scan_new;     // for routing
    */
    config::bep_config_t bep_config;
    std::uint32_t hasher_threads;
    model::cluster_ptr_t cluster;
    model::folders_map_t &folders;
    //device2addr_map_t device2addr_map;
    /*
    addr2device_map_t addr2device_map;
    scan_folders_map_t scan_folders_map;
    */
};

} // namespace net
} // namespace syncspirit
