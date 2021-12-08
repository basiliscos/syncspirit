#pragma once

#include "messages.h"
#include "../model/misc/file_iterator.h"
#include "../model/misc/block_iterator.h"
#include "../fs/messages.h"
#include "../hasher/messages.h"
#include "../utils/log.h"
#include <unordered_set>
#include <optional>

namespace syncspirit {
namespace net {

namespace bfs = boost::filesystem;

namespace payload {

struct ready_signal_t {};

}

namespace message {
using ready_signal_t = r::message_t<payload::ready_signal_t>;
}

struct controller_actor_config_t : r::actor_config_t {
    config::bep_config_t bep_config;
    model::cluster_ptr_t cluster;
    model::device_ptr_t peer;
    r::address_ptr_t peer_addr;
    pt::time_duration request_timeout;
    size_t blocks_max_kept = 15;
    size_t blocks_max_requested = 8;
};

template <typename Actor> struct controller_actor_config_builder_t : r::actor_config_builder_t<Actor> {
    using builder_t = typename Actor::template config_builder_t<Actor>;
    using parent_t = r::actor_config_builder_t<Actor>;
    using parent_t::parent_t;

    builder_t &&bep_config(const config::bep_config_t &value) &&noexcept {
        parent_t::config.bep_config = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&cluster(const model::cluster_ptr_t &value) &&noexcept {
        parent_t::config.cluster = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&peer(const model::device_ptr_t &value) &&noexcept {
        parent_t::config.peer = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&peer_addr(const r::address_ptr_t &value) &&noexcept {
        parent_t::config.peer_addr = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&request_timeout(const pt::time_duration &value) &&noexcept {
        parent_t::config.request_timeout = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&blocks_max_kept(size_t value) &&noexcept {
        parent_t::config.blocks_max_kept = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&blocks_max_requested(size_t value) &&noexcept {
        parent_t::config.blocks_max_requested = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
};

struct controller_actor_t : public r::actor_base_t {
    using config_t = controller_actor_config_t;
    template <typename Actor> using config_builder_t = controller_actor_config_builder_t<Actor>;

    controller_actor_t(config_t &config);
    void configure(r::plugin::plugin_base_t &plugin) noexcept override;
    void on_start() noexcept override;
    void shutdown_start() noexcept override;
    void shutdown_finish() noexcept override;
    bool on_unlink(const r::address_ptr_t &peer_addr) noexcept override;

    struct folder_updater_t {
        model::device_ptr_t peer;
        virtual const std::string &id() noexcept = 0;
        virtual model::folder_info_ptr_t update(model::folder_t &folder) noexcept = 0;
    };

  private:
    struct clone_block_t {
        model::block_info_ptr_t block;
        model::file_info_ptr_t source;
        size_t source_index;
        size_t target_index;
    };
    using blocks_queue_t = std::list<r::intrusive_ptr_t<message::block_response_t>>;
    using clone_queue_t = std::list<clone_block_t>;
#if 0
    struct write_info_t {
        using opened_file_t = fs::opened_file_t;
        write_info_t(const model::file_info_ptr_t &file) noexcept;

        blocks_queue_t validated_blocks;
        clone_queue_t clone_queue;
        opened_file_t sink;
        model::file_info_ptr_t file;
        std::uint_fast32_t pending_blocks;
        size_t blocks_left;
        bool opening;

        bool done() const noexcept;
        bool complete() const noexcept;
    };
#endif

    using peers_map_t = std::unordered_map<r::address_ptr_t, model::device_ptr_t>;
#if 0
    using write_map_t = std::unordered_map<std::string, write_info_t>;
    using write_it_t = typename write_map_t::iterator;
#endif

    enum class ImmediateResult { DONE, NON_IMMEDIATE, ERROR };

    using unlink_request_t = r::message::unlink_request_t;
    using unlink_request_ptr_t = r::intrusive_ptr_t<unlink_request_t>;
    using unlink_requests_t = std::vector<unlink_request_ptr_t>;

    bool on_unlink(unlink_request_t &message) noexcept;
    void on_forward(message::forwarded_message_t &message) noexcept;
    void on_ready(message::ready_signal_t &message) noexcept;
    void on_block(message::block_response_t &message) noexcept;
    void on_validation(hasher::message::validation_response_t &res) noexcept;
/*
    void on_open(fs::message::open_response_t &res) noexcept;
    void on_close(fs::message::close_response_t &res) noexcept;
    void on_clone(fs::message::clone_response_t &res) noexcept;
    void on_store_folder_info(message::store_folder_info_response_t &message) noexcept;
    void on_store_file_info(message::store_file_response_t &message) noexcept;
    void on_new_folder(message::store_new_folder_notify_t &message) noexcept;
*/
    void on_file_update(message::file_update_notify_t &message) noexcept;

    void on_message(proto::message::ClusterConfig &message) noexcept;
    void on_message(proto::message::Index &message) noexcept;
    void on_message(proto::message::IndexUpdate &message) noexcept;
    void on_message(proto::message::Request &message) noexcept;
    void on_message(proto::message::DownloadProgress &message) noexcept;

    void request_block(const model::file_block_t &block) noexcept;
#if 0
    write_info_t &record_block_data(model::file_info_ptr_t &file, std::size_t block_index) noexcept;
    void update(proto::ClusterConfig &config) noexcept;
    void update(folder_updater_t &&updater) noexcept;
    void process(write_it_t it) noexcept;
#endif
    void clone_block(const model::file_block_t &block, model::file_block_t &info) noexcept;
    ImmediateResult process_immediately() noexcept;
    void ready() noexcept;

    model::cluster_ptr_t cluster;
    model::device_ptr_t peer;
    model::folder_ptr_t folder;
    r::address_ptr_t coordinator;
    r::address_ptr_t peer_addr;
#if 0
    r::address_ptr_t db;
    r::address_ptr_t file_addr;
#endif
    r::address_ptr_t hasher_proxy;
    r::address_ptr_t open_reading; /* for routing */
    pt::time_duration request_timeout;
#if 0
    payload::cluster_config_ptr_t peer_cluster_config;
#endif
    model::ignored_folders_map_t *ignored_folders;
    peers_map_t peers_map;
#if 0
    write_map_t write_map;
#endif

    //model::file_iterator_ptr_t file_iterator;
    model::file_info_ptr_t current_file;
    //model::blocks_interator_t block_iterator;
    // generic
    std::uint_fast32_t blocks_requested = 0;
    std::uint_fast32_t blocks_kept = 0;

    int64_t request_pool;
    size_t blocks_max_kept;
    size_t blocks_max_requested;
    utils::logger_t log;
    unlink_requests_t unlink_requests;
};

} // namespace net
} // namespace syncspirit
