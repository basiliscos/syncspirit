// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include "messages.h"
#include "config/db.h"
#include "model/messages.h"
#include "model/cluster.h"
#include "model/diff/cluster_visitor.h"
#include "mdbx.h"
#include "utils/log.h"
#include "db/transaction.h"
#include "db/utils.h"
#include "utils/bytes_comparator.hpp"

namespace syncspirit {
namespace net {

namespace outcome = boost::outcome_v2;
namespace bfs = std::filesystem;

struct db_actor_config_t : r::actor_config_t {
    bfs::path db_dir;
    config::db_config_t db_config;
    model::cluster_ptr_t cluster;
    size_t uncommitted_threshold = {100};
};

template <typename Actor> struct db_actor_config_builder_t : r::actor_config_builder_t<Actor> {
    using builder_t = typename Actor::template config_builder_t<Actor>;
    using parent_t = r::actor_config_builder_t<Actor>;
    using parent_t::parent_t;

    builder_t &&db_dir(const bfs::path &value) && noexcept {
        parent_t::config.db_dir = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&cluster(const model::cluster_ptr_t &value) && noexcept {
        parent_t::config.cluster = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&db_config(const config::db_config_t &value) && noexcept {
        parent_t::config.db_config = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
};

struct SYNCSPIRIT_API db_actor_t : public r::actor_base_t, private model::diff::cluster_visitor_t {
    using config_t = db_actor_config_t;
    template <typename Actor> using config_builder_t = db_actor_config_builder_t<Actor>;

    static void delete_tx(MDBX_txn *) noexcept;

    db_actor_t(config_t &config);
    ~db_actor_t();
    void configure(r::plugin::plugin_base_t &plugin) noexcept override;
    void on_start() noexcept override;
    void shutdown_start() noexcept override;
    void shutdown_finish() noexcept override;

    template <typename T> auto &access() noexcept;

    using transaction_ptr_t = std::unique_ptr<db::transaction_t>;
    using thread_id_t = std::thread::id;
    using known_hashes_t = std::unordered_set<utils::bytes_view_t>;
    using unique_keys_t = std::set<utils::bytes_t, utils::bytes_comparator_t>;
    using folder_infos_uuids_t = std::unordered_set<std::string>;

    struct payload {
        struct commit_t {
            commit_t(db::transaction_t txn) noexcept;
            commit_t(const commit_t &) = delete;
            commit_t(commit_t &&) = default;
            ~commit_t() noexcept;

            outcome::result<void> commit() noexcept;

            db::transaction_t txn;
            thread_id_t thread_id;
        };

        struct partial_load_t {
            model::diff::cluster_diff_ptr_t diff;
            model::diff::cluster_diff_t *next;
            known_hashes_t known_hashes;
            db::container_t blocks;
            db::container_t::pointer block_next;
            db::container_t files;
            db::container_t::pointer files_next;
            folder_infos_uuids_t folder_infos_uuids;
            unique_keys_t corrupted_files;
            db::transaction_t txn;
        };
    };

    using commit_message_t = r::message_t<payload::commit_t>;
    using partial_load_t = r::message_t<payload::partial_load_t>;

  private:
    void open() noexcept;
    outcome::result<db::transaction_t *> get_txn() noexcept;
    outcome::result<void> commit_on_demand() noexcept;
    outcome::result<void> force_commit() noexcept;

    void on_cluster_load_trigger(message::load_cluster_trigger_t &) noexcept;
    void on_commit(commit_message_t &) noexcept;
    void on_model_update(model::message::model_update_t &) noexcept;
    void on_db_info(message::db_info_request_t &) noexcept;
    void on_controller_up(net::message::controller_up_t &message) noexcept;
    void on_controller_down(net::message::controller_down_t &message) noexcept;
    void on_patrial_load(partial_load_t &message) noexcept;

    void extracted(const model::folder_info_t &folder_info);
    outcome::result<void> save_folder_info(const model::folder_info_t &, void *) noexcept;
    outcome::result<void> remove(const model::diff::modify::generic_remove_t &, void *) noexcept;

    outcome::result<void> operator()(const model::diff::advance::advance_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::contact::peer_state_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::contact::ignored_connected_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::contact::unknown_connected_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::load::remove_corrupted_files_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::add_blocks_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::add_ignored_device_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::add_pending_device_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::add_pending_folders_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::remove_blocks_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::remove_files_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::remove_folder_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::remove_folder_infos_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::remove_ignored_device_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::remove_peer_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::remove_pending_device_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::remove_pending_folders_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::update_peer_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::upsert_folder_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::upsert_folder_info_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::peer::cluster_update_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::peer::update_folder_t &, void *) noexcept override;

    r::address_ptr_t coordinator;
    r::address_ptr_t bouncer;
    r::address_ptr_t sink;
    utils::logger_t log;
    MDBX_env *env;
    bfs::path db_dir;
    config::db_config_t db_config;
    model::cluster_ptr_t cluster;
    transaction_ptr_t txn_holder;
    std::int_fast32_t uncommitted;
};

} // namespace net
} // namespace syncspirit
