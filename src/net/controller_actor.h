// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2026 Ivan Baidakou

#pragma once

#include "messages.h"
#include "model_actor.hpp"
#include "model/messages.h"
#include "model/diff/cluster_visitor.h"
#include "model/diff/diff_assembler.h"
#include "model/misc/file_iterator.h"
#include "model/misc/block_iterator.h"
#include "model/misc/postponed_files.h"
#include "model/misc/updates_streamer.h"
#include "model/misc/sequencer.h"
#include "hasher/messages.h"
#include "hasher/hasher_plugin.h"
#include "fs/messages.h"

#include <unordered_map>
#include <optional>
#include <list>

namespace syncspirit {
namespace net {

namespace bfs = std::filesystem;
namespace outcome = boost::outcome_v2;

struct SYNCSPIRIT_API controller_actor_t final : public model_actor_t<r::actor_base_t>,
                                                 private model::diff::cluster_visitor_t {
    using parent_t = model_actor_t<r::actor_base_t>;

    struct config_t : parent_t::config_t {
        using base_t = model_actor_t<r::actor_base_t>::config_t;
        using base_t::base_t;
        int64_t request_pool;
        model::sequencer_ptr_t sequencer;
        model::device_ptr_t peer;
        r::address_ptr_t peer_addr;
        uint32_t hasher_threads;
        uint32_t blocks_max_requested = 0;
        uint32_t outgoing_buffer_max = 0;
        std::uint32_t advances_per_iteration = 10;
        bfs::path default_path;
    };

    template <typename Actor> struct config_builder_t : parent_t::template config_builder_t<Actor> {
        using builder_t = typename Actor::template config_builder_t<Actor>;
        using base_t = parent_t::template config_builder_t<Actor>;
        using base_t::base_t;

        builder_t &&request_pool(int64_t value) && noexcept {
            base_t::config.request_pool = value;
            return std::move(*static_cast<typename base_t::builder_t *>(this));
        }

        builder_t &&peer(const model::device_ptr_t &value) && noexcept {
            base_t::config.peer = value;
            return std::move(*static_cast<typename base_t::builder_t *>(this));
        }

        builder_t &&peer_addr(const r::address_ptr_t &value) && noexcept {
            base_t::config.peer_addr = value;
            return std::move(*static_cast<typename base_t::builder_t *>(this));
        }

        builder_t &&blocks_max_kept(size_t value) && noexcept {
            base_t::config.blocks_max_kept = value;
            return std::move(*static_cast<typename base_t::builder_t *>(this));
        }

        builder_t &&hasher_threads(uint32_t value) && noexcept {
            base_t::config.hasher_threads = value;
            return std::move(*static_cast<typename base_t::builder_t *>(this));
        }

        builder_t &&blocks_max_requested(uint32_t value) && noexcept {
            base_t::config.blocks_max_requested = value;
            return std::move(*static_cast<typename base_t::builder_t *>(this));
        }

        builder_t &&advances_per_iteration(uint32_t value) && noexcept {
            base_t::config.advances_per_iteration = value;
            return std::move(*static_cast<typename base_t::builder_t *>(this));
        }

        builder_t &&outgoing_buffer_max(uint32_t value) && noexcept {
            base_t::config.outgoing_buffer_max = value;
            return std::move(*static_cast<typename base_t::builder_t *>(this));
        }

        builder_t &&sequencer(model::sequencer_ptr_t value) && noexcept {
            base_t::config.sequencer = std::move(value);
            return std::move(*static_cast<typename base_t::builder_t *>(this));
        }

        builder_t &&default_path(const bfs::path &value) && noexcept {
            base_t::config.default_path = value;
            return std::move(*static_cast<typename base_t::builder_t *>(this));
        }
    };

    // clang-format off
    using plugins_list_t = std::tuple<
        r::plugin::address_maker_plugin_t,
        r::plugin::lifetime_plugin_t,
        r::plugin::init_shutdown_plugin_t,
        r::plugin::link_server_plugin_t,
        r::plugin::link_client_plugin_t,
        hasher::hasher_plugin_t,
        r::plugin::resources_plugin_t,
        r::plugin::starter_plugin_t
    >;
    // clang-format on

    controller_actor_t(config_t &config);
    ~controller_actor_t();
    void configure(r::plugin::plugin_base_t &plugin) noexcept override;
    void on_start() noexcept override;
    void shutdown_start() noexcept override;
    void shutdown_finish() noexcept override;
    void visit(const model::diff::cluster_diff_t &, model::payload::apply_context_t &) noexcept override;

  private:
    struct block_ack_context_t;
    struct stack_context_t;
    struct update_context_t;

    using peers_map_t = std::unordered_map<r::address_ptr_t, model::device_ptr_t>;
    using io_queue_t = std::list<fs::payload::io_command_t>;

    struct folder_synchronization_t;
    using folder_sync_ptr_t = std::unique_ptr<folder_synchronization_t>;

    using synchronizing_folders_t = std::unordered_map<model::folder_ptr_t, folder_sync_ptr_t>;
    using synchronizing_files_t = std::unordered_map<utils::bytes_view_t, model::file_info_t::guard_t>;
    using postponed_files_t = std::unordered_set<model::file_info_ptr_t>;
    using updates_streamer_ptr_t = std::unique_ptr<model::updates_streamer_t>;
    using tx_size_ptr_t = payload::controller_up_t::tx_size_ptr_t;
    using block_requests_t = std::vector<fs::payload::extendended_context_prt_t>;

    void on_peer_down(message::peer_down_t &message) noexcept;
    void on_forward(message::forwarded_messages_t &message) noexcept;

    void on_digest(hasher::message::digest_t &res) noexcept;
    void preprocess_block(model::file_block_t &block, const model::folder_info_t &source_folder,
                          stack_context_t &) noexcept;
    void on_tx_signal(net::message::tx_signal_t &message) noexcept;
    void on_postprocess_io(fs::message::io_commands_t &) noexcept;
    void on_fs_predown(message::fs_predown_t &message) noexcept;
    void on_fs_ack_timer(r::request_id_t, bool cancelled) noexcept;

    void on_message(proto::ClusterConfig &message, stack_context_t &) noexcept;
    void on_message(proto::Index &message, stack_context_t &) noexcept;
    void on_message(proto::IndexUpdate &message, stack_context_t &) noexcept;
    void on_message(proto::Request &message, stack_context_t &) noexcept;
    void on_message(proto::Response &message, stack_context_t &) noexcept;

    void postprocess_io(fs::payload::block_request_t &, stack_context_t &) noexcept;
    void postprocess_io(fs::payload::remote_copy_t &, stack_context_t &) noexcept;
    void postprocess_io(fs::payload::append_block_t &, stack_context_t &) noexcept;
    void postprocess_io(fs::payload::finish_file_t &, stack_context_t &) noexcept;
    void postprocess_io(fs::payload::clone_block_t &, stack_context_t &) noexcept;

    void request_block(const model::file_block_t &block) noexcept;
    void pull_next(stack_context_t &) noexcept;
    void push_pending(stack_context_t &) noexcept;
    void send_cluster_config(stack_context_t &) noexcept;
    void send_new_indices() noexcept;

    void io_advance(model::advance_action_t action, model::file_info_t &peer_file, model::folder_info_t &peer_folder,
                    stack_context_t &);
    void io_append_block(model::file_info_t &, model::folder_info_t &, uint32_t block_index, utils::bytes_t data,
                         stack_context_t &);
    void io_clone_block(const model::file_block_t &file_block, model::folder_info_t &target_fi, stack_context_t &);
    void io_finish_file(model::file_info_t *, model::file_info_t &, model::folder_info_t &, model::advance_action_t,
                        stack_context_t &);
    auto io_make_request_block(model::file_info_t &, model::folder_info_t &, proto::Request)
        -> fs::payload::io_command_t;

    void acquire_block(const model::file_block_t &block, const model::folder_info_t &folder_info,
                       stack_context_t &) noexcept;
    model::block_info_ptr_t release_block(std::string_view folder_id, utils::bytes_view_t hash,
                                          stack_context_t &) noexcept;
    folder_synchronization_t &get_sync_info(model::folder_t *folder) noexcept;
    folder_synchronization_t &get_sync_info(std::string_view folder_id) noexcept;
    void cancel_sync(model::file_info_t *) noexcept;
    bool is_unflushed(model::file_info_t *peer_file, model::folder_info_t &peer_folder) noexcept;

    outcome::result<void> operator()(const model::diff::advance::advance_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::contact::peer_state_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::block_ack_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::mark_reachable_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::remove_peer_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::remove_files_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::remove_folder_infos_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::upsert_folder_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::upsert_folder_info_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::peer::cluster_update_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::peer::update_folder_t &, void *) noexcept override;

    model::sequencer_ptr_t sequencer;
    model::device_ptr_t peer;
    model::device_state_t peer_state;
    r::address_ptr_t peer_address;
    r::address_ptr_t fs_addr;
    model::ignored_folders_map_t *ignored_folders;
    // generic
    tx_size_ptr_t outgoing_buffer;
    std::uint_fast32_t rx_blocks_requested;
    std::uint_fast32_t tx_blocks_requested;
    uint32_t outgoing_buffer_max;

    int64_t request_pool;
    uint32_t blocks_max_kept;
    uint32_t hasher_threads;
    uint32_t blocks_max_requested;
    uint32_t advances_per_iteration;
    bfs::path default_path;
    updates_streamer_ptr_t updates_streamer;
    model::file_iterator_ptr_t file_iterator;
    model::block_iterator_ptr_t block_iterator;
    synchronizing_folders_t synchronizing_folders;
    synchronizing_files_t synchronizing_files;
    model::postponed_files_t postponed_files;
    io_queue_t block_write_queue;
    io_queue_t block_read_queue;
    block_requests_t block_requests;
    std::uint_fast32_t block_requests_next = 0;
    std::optional<r::request_id_t> fs_ack_timer;
    hasher::hasher_plugin_t *hasher;
    bool announced;

    friend stack_context_t;
};

} // namespace net
} // namespace syncspirit
