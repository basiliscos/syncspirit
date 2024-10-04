// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include "messages.h"
#include "model/messages.h"
#include "model/cluster.h"
#include "model/diff/local/custom.h"
#include "model/diff/modify/block_transaction.h"
#include "model/diff/cluster_visitor.h"
#include "model/misc/file_iterator.h"
#include "model/misc/block_iterator.h"
#include "model/misc/updates_streamer.h"
#include "model/misc/sequencer.h"
#include "hasher/messages.h"
#include "utils/log.h"
#include "fs/messages.h"

#include <unordered_set>
#include <unordered_map>
#include <deque>

namespace syncspirit {
namespace net {

namespace bfs = boost::filesystem;
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

    builder_t &&outgoing_buffer_max(uint32_t value) && noexcept {
        parent_t::config.outgoing_buffer_max = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&sequencer(model::sequencer_ptr_t value) && noexcept {
        parent_t::config.sequencer = std::move(value);
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
    enum substate_t { none = 0, iterating_files, iterating_blocks };

    struct file_lock_t : model::arc_base_t<file_lock_t> {
        model::file_info_ptr_t file;
        controller_actor_t &controller;
        model::diff::cluster_diff_ptr_t *diff_storage;
        bool is_locked;

        file_lock_t(model::file_info_ptr_t file, controller_actor_t &controller) noexcept;
        ~file_lock_t();
    };

    struct pull_signal_t final : model::diff::local::custom_t {
        pull_signal_t(void *controller) noexcept;
        outcome::result<void> visit(model::diff::cluster_visitor_t &, void *) const noexcept override;
        void *controller;
    };

    struct forget_file_t final : model::diff::local::custom_t {
        forget_file_t(void *controller, std::string full_name) noexcept;
        outcome::result<void> visit(model::diff::cluster_visitor_t &, void *) const noexcept override;
        void *controller;
        std::string full_name;
    };

    using file_lock_ptr_t = r::intrusive_ptr_t<file_lock_t>;
    using peers_map_t = std::unordered_map<r::address_ptr_t, model::device_ptr_t>;
    using locked_files_t = std::unordered_map<std::string, file_lock_ptr_t>;
    using unlink_request_t = r::message::unlink_request_t;
    using unlink_request_ptr_t = r::intrusive_ptr_t<unlink_request_t>;
    using unlink_requests_t = std::vector<unlink_request_ptr_t>;
    using block_write_queue_t = std::deque<model::diff::cluster_diff_ptr_t>;
    using dispose_callback_t = model::diff::modify::block_transaction_t::dispose_callback_t;
    using synchronizing_folders_t = std::unordered_map<model::folder_ptr_t, int>;

    void on_termination(message::termination_signal_t &message) noexcept;
    void on_forward(message::forwarded_message_t &message) noexcept;
    void on_block(message::block_response_t &message) noexcept;
    void on_validation(hasher::message::validation_response_t &res) noexcept;
    void preprocess_block(model::file_block_t &block) noexcept;
    void on_model_update(model::message::model_update_t &message) noexcept;
    void on_transfer_push(message::transfer_push_t &message) noexcept;
    void on_transfer_pop(message::transfer_pop_t &message) noexcept;
    void on_block_response(fs::message::block_response_t &message) noexcept;

    void on_message(proto::message::ClusterConfig &message) noexcept;
    void on_message(proto::message::Index &message) noexcept;
    void on_message(proto::message::IndexUpdate &message) noexcept;
    void on_message(proto::message::Request &message) noexcept;
    void on_message(proto::message::DownloadProgress &message) noexcept;

    void on_cutom(const pull_signal_t &diff) noexcept;
    void on_cutom(const forget_file_t &diff) noexcept;

    void request_block(const model::file_block_t &block) noexcept;
    void pull_next() noexcept;
    void pull_ready() noexcept;
    void push_pending() noexcept;
    void send_cluster_config() noexcept;
    void push_block_write(model::diff::cluster_diff_ptr_t block) noexcept;
    void process_block_write() noexcept;
    dispose_callback_t make_callback() noexcept;

    void assign_diff(model::diff::cluster_diff_ptr_t) noexcept;
    void send_diff();

    model::file_info_ptr_t next_file() noexcept;

    outcome::result<void> operator()(const model::diff::peer::cluster_update_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::peer::update_folder_t &, void *) noexcept override;
    // outcome::result<void> operator()(const model::diff::local::custom_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::local::update_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::clone_file_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::lock_file_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::finish_file_ack_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::share_folder_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::unshare_folder_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::mark_reachable_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::block_ack_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::block_rej_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::remove_peer_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::upsert_folder_info_t &, void *) noexcept override;

    model::sequencer_ptr_t sequencer;
    model::cluster_ptr_t cluster;
    model::device_ptr_t peer;
    model::folder_ptr_t folder;
    r::address_ptr_t coordinator;
    r::address_ptr_t peer_addr;
    r::address_ptr_t hasher_proxy;
    r::address_ptr_t fs_addr;
    r::address_ptr_t open_reading; /* for routing */
    pt::time_duration request_timeout;
    model::ignored_folders_map_t *ignored_folders;
    // generic
    std::uint_fast32_t rx_blocks_requested;
    std::uint_fast32_t tx_blocks_requested;
    uint32_t outgoing_buffer;
    uint32_t outgoing_buffer_max;

    // diff
    model::diff::cluster_diff_ptr_t diff;
    model::diff::cluster_diff_t *current_diff;
    std::uint32_t planned_pulls;

    int64_t request_pool;
    uint32_t blocks_max_kept;
    uint32_t blocks_max_requested;
    utils::logger_t log;
    unlink_requests_t unlink_requests;
    model::file_info_ptr_t file;
    model::file_iterator_ptr_t file_iterator;
    model::block_iterator_ptr_t block_iterator;
    model::updates_streamer_t updates_streamer;
    int substate = substate_t::none;
    locked_files_t file_locks;
    model::block_infos_map_t block_locks;
    synchronizing_folders_t synchronizing_folders;
    block_write_queue_t block_write_queue;
};

} // namespace net
} // namespace syncspirit
