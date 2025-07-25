// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include "messages.h"
#include "model/messages.h"
#include "model/cluster.h"
#include "model/diff/local/custom.h"
#include "model/diff/cluster_visitor.h"
#include "model/misc/file_iterator.h"
#include "model/misc/block_iterator.h"
#include "model/misc/updates_streamer.h"
#include "model/misc/sequencer.h"
#include "hasher/messages.h"
#include "utils/log.h"
#include "fs/messages.h"

#include <unordered_map>
#include <optional>
#include <list>

namespace syncspirit {
namespace net {

namespace bfs = std::filesystem;
namespace outcome = boost::outcome_v2;

struct controller_actor_config_t : r::actor_config_t {
    int64_t request_pool;
    model::cluster_ptr_t cluster;
    model::sequencer_ptr_t sequencer;
    model::device_ptr_t peer;
    r::address_ptr_t peer_addr;
    pt::time_duration request_timeout;
    uint32_t blocks_max_requested = 8;
    uint32_t outgoing_buffer_max = 0;
    std::uint32_t advances_per_iteration = 10;
    bfs::path default_path;
};

template <typename Actor> struct controller_actor_config_builder_t : r::actor_config_builder_t<Actor> {
    using builder_t = typename Actor::template config_builder_t<Actor>;
    using parent_t = r::actor_config_builder_t<Actor>;
    using parent_t::parent_t;

    builder_t &&request_pool(int64_t value) && noexcept {
        parent_t::config.request_pool = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&cluster(const model::cluster_ptr_t &value) && noexcept {
        parent_t::config.cluster = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&peer(const model::device_ptr_t &value) && noexcept {
        parent_t::config.peer = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&peer_addr(const r::address_ptr_t &value) && noexcept {
        parent_t::config.peer_addr = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&request_timeout(const pt::time_duration &value) && noexcept {
        parent_t::config.request_timeout = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&blocks_max_kept(size_t value) && noexcept {
        parent_t::config.blocks_max_kept = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&blocks_max_requested(uint32_t value) && noexcept {
        parent_t::config.blocks_max_requested = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&advances_per_iteration(uint32_t value) && noexcept {
        parent_t::config.advances_per_iteration = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&outgoing_buffer_max(uint32_t value) && noexcept {
        parent_t::config.outgoing_buffer_max = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&sequencer(model::sequencer_ptr_t value) && noexcept {
        parent_t::config.sequencer = std::move(value);
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&default_path(const bfs::path &value) && noexcept {
        parent_t::config.default_path = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
};

struct SYNCSPIRIT_API controller_actor_t : public r::actor_base_t, private model::diff::cluster_visitor_t {
    using config_t = controller_actor_config_t;
    template <typename Actor> using config_builder_t = controller_actor_config_builder_t<Actor>;

    controller_actor_t(config_t &config);
    void configure(r::plugin::plugin_base_t &plugin) noexcept override;
    void on_start() noexcept override;
    void shutdown_start() noexcept override;
    void shutdown_finish() noexcept override;

  private:
    struct pull_signal_t final : model::diff::local::custom_t {
        pull_signal_t(void *controller) noexcept;
        outcome::result<void> visit(model::diff::cluster_visitor_t &, void *) const noexcept override;
        void *controller;
    };

    using peers_map_t = std::unordered_map<r::address_ptr_t, model::device_ptr_t>;
    using block_write_queue_t = std::list<model::diff::cluster_diff_ptr_t>;
    using block_read_queue_t = std::list<proto::Request>;

    struct folder_synchronization_t : model::arc_base_t<folder_synchronization_t> {
        using block_set_t = std::unordered_map<utils::bytes_view_t, model::block_info_ptr_t>;
        folder_synchronization_t(controller_actor_t &controller, model::folder_t &folder) noexcept;
        ~folder_synchronization_t();
        void reset() noexcept;

        void start_fetching(model::block_info_t *) noexcept;
        void finish_fetching(utils::bytes_view_t hash) noexcept;

        void start_sync() noexcept;
        void finish_sync() noexcept;

      private:
        controller_actor_t &controller;
        model::folder_ptr_t folder;
        block_set_t blocks;
        bool synchronizing;
    };
    using folder_synchronization_ptr_t = model::intrusive_ptr_t<folder_synchronization_t>;
    using synchronizing_folders_t = std::unordered_map<model::folder_ptr_t, folder_synchronization_ptr_t>;
    using synchronizing_files_t = std::unordered_map<utils::bytes_view_t, model::file_info_t::guard_ptr_t>;
    using updates_streamer_ptr_t = std::unique_ptr<model::updates_streamer_t>;
    using tx_size_ptr_t = payload::controller_up_t::tx_size_ptr_t;

    void send_to_peer(utils::bytes_t data) noexcept;

    void on_peer_down(message::peer_down_t &message) noexcept;
    void on_forward(message::forwarded_message_t &message) noexcept;
    void on_block(message::block_response_t &message) noexcept;
    void on_validation(hasher::message::validation_response_t &res) noexcept;
    void preprocess_block(model::file_block_t &block) noexcept;
    void on_model_update(model::message::model_update_t &message) noexcept;
    void on_tx_signal(net::message::tx_signal_t &message) noexcept;
    void on_block_response(fs::message::block_response_t &message) noexcept;
    void on_fs_predown(message::fs_predown_t &message) noexcept;
    void on_fs_ack_timer(r::request_id_t, bool cancelled) noexcept;

    void on_message(proto::ClusterConfig &message) noexcept;
    void on_message(proto::Index &message) noexcept;
    void on_message(proto::IndexUpdate &message) noexcept;
    void on_message(proto::Request &message) noexcept;
    void on_message(proto::DownloadProgress &message) noexcept;

    void on_custom(const pull_signal_t &diff) noexcept;

    void request_block(const model::file_block_t &block) noexcept;
    void pull_next() noexcept;
    void pull_ready() noexcept;
    void push_pending() noexcept;
    void send_cluster_config() noexcept;
    void send_new_indices() noexcept;
    void push_block_write(model::diff::cluster_diff_ptr_t block) noexcept;
    void process_block_write() noexcept;

    void push(model::diff::cluster_diff_ptr_t diff) noexcept;
    void send_diff() noexcept;
    void acquire_block(const model::file_block_t &block) noexcept;
    void release_block(std::string_view folder_id, utils::bytes_view_t hash) noexcept;
    folder_synchronization_ptr_t get_sync_info(model::folder_t *folder) noexcept;
    folder_synchronization_ptr_t get_sync_info(std::string_view folder_id) noexcept;
    void cancel_sync(model::file_info_t *) noexcept;

    outcome::result<void> operator()(const model::diff::advance::advance_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::contact::peer_state_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::add_remote_folder_infos_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::block_ack_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::block_rej_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::mark_reachable_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::remove_peer_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::remove_files_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::remove_folder_infos_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::upsert_folder_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::upsert_folder_info_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::peer::cluster_update_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::peer::update_folder_t &, void *) noexcept override;

    model::sequencer_ptr_t sequencer;
    model::cluster_ptr_t cluster;
    model::device_ptr_t peer;
    model::device_state_t peer_state;
    r::address_ptr_t coordinator;
    r::address_ptr_t peer_address;
    r::address_ptr_t hasher_proxy;
    r::address_ptr_t fs_addr;
    r::address_ptr_t open_reading; /* for routing */
    pt::time_duration request_timeout;
    model::ignored_folders_map_t *ignored_folders;
    // generic
    tx_size_ptr_t outgoing_buffer;
    std::uint_fast32_t rx_blocks_requested;
    std::uint_fast32_t tx_blocks_requested;
    uint32_t outgoing_buffer_max;

    // diff
    model::diff::cluster_diff_ptr_t diff;
    model::diff::cluster_diff_t *current_diff;
    std::uint32_t planned_pulls;

    int64_t request_pool;
    uint32_t blocks_max_kept;
    uint32_t blocks_max_requested;
    uint32_t advances_per_iteration;
    bfs::path default_path;
    updates_streamer_ptr_t updates_streamer;
    utils::logger_t log;
    model::file_iterator_ptr_t file_iterator;
    model::block_iterator_ptr_t block_iterator;
    synchronizing_folders_t synchronizing_folders;
    synchronizing_files_t synchronizing_files;
    block_write_queue_t block_write_queue;
    block_read_queue_t block_read_queue;
    std::optional<r::request_id_t> fs_ack_timer;
    bool announced;
};

} // namespace net
} // namespace syncspirit
