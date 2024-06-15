// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include "messages.h"
#include "config/db.h"
#include "model/messages.h"
#include "model/diff/cluster_visitor.h"
#include "mdbx.h"
#include "utils/log.h"
#include "db/transaction.h"

namespace syncspirit {
namespace net {

namespace outcome = boost::outcome_v2;

struct db_actor_config_t : r::actor_config_t {
    std::string db_dir;
    config::db_config_t db_config;
    model::cluster_ptr_t cluster;
    size_t uncommitted_threshold = {100};
};

template <typename Actor> struct db_actor_config_builder_t : r::actor_config_builder_t<Actor> {
    using builder_t = typename Actor::template config_builder_t<Actor>;
    using parent_t = r::actor_config_builder_t<Actor>;
    using parent_t::parent_t;

    builder_t &&db_dir(const std::string &value) && noexcept {
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
    void shutdown_finish() noexcept override;

    template <typename T> auto &access() noexcept;

  private:
    using transaction_ptr_t = std::unique_ptr<db::transaction_t>;

    void open() noexcept;
    outcome::result<db::transaction_t *> get_txn() noexcept;
    outcome::result<void> commit(bool force = false) noexcept;

    void on_cluster_load(message::load_cluster_request_t &message) noexcept;
    void on_model_update(model::message::model_update_t &message) noexcept;
    outcome::result<void> save(db::transaction_t &txn, model::folder_info_ptr_t &folder_info) noexcept;
    outcome::result<void> operator()(const model::diff::modify::add_unknown_folders_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::create_folder_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::share_folder_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::generic_remove_t &) noexcept;
    outcome::result<void> operator()(const model::diff::modify::remove_blocks_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::remove_files_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::remove_folder_infos_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::remove_unknown_folders_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::remove_peer_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::unshare_folder_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::update_peer_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::update_folder_info_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::clone_file_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::finish_file_ack_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::local_update_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::peer::cluster_update_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::peer::update_folder_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::peer::peer_state_t &, void *) noexcept override;

    r::address_ptr_t coordinator;
    utils::logger_t log;
    MDBX_env *env;
    std::string db_dir;
    config::db_config_t db_config;
    model::cluster_ptr_t cluster;
    transaction_ptr_t txn_holder;
    std::uint32_t txn_counter;
    size_t uncommitted;
};

} // namespace net
} // namespace syncspirit
