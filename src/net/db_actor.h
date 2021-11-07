#pragma once

#include "messages.h"
#include "model/diff/diff_visitor.h"
#include "mdbx.h"
#include "../utils/log.h"
#include "../db/transaction.h"
#include <optional>

namespace syncspirit {
namespace net {

namespace outcome = boost::outcome_v2;

struct db_actor_config_t : r::actor_config_t {
    std::string db_dir;
    model::cluster_ptr_t cluster;
};

template <typename Actor> struct db_actor_config_builder_t : r::actor_config_builder_t<Actor> {
    using builder_t = typename Actor::template config_builder_t<Actor>;
    using parent_t = r::actor_config_builder_t<Actor>;
    using parent_t::parent_t;

    builder_t &&db_dir(const std::string &value) &&noexcept {
        parent_t::config.db_dir = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&cluster(const model::cluster_ptr_t &value) &&noexcept {
        parent_t::config.cluster = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
};

struct db_actor_t : public r::actor_base_t, private model::diff::diff_visitor_t {
    using config_t = db_actor_config_t;
    template <typename Actor> using config_builder_t = db_actor_config_builder_t<Actor>;

    static void delete_tx(MDBX_txn *) noexcept;

    db_actor_t(config_t &config);
    ~db_actor_t();
    void configure(r::plugin::plugin_base_t &plugin) noexcept override;
    void on_start() noexcept override;

    template <typename T> auto &access() noexcept;

  private:
    void on_cluster_load(message::load_cluster_request_t &message) noexcept;
    void on_model_update(message::model_update_t &message) noexcept;
    void open() noexcept;
    outcome::result<void> save(db::transaction_t &txn, model::folder_info_ptr_t &folder_info) noexcept;

    outcome::result<void> operator()(const model::diff::modify::create_folder_t &) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::share_folder_t &) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::update_peer_t &) noexcept override;

    r::address_ptr_t coordinator;
    utils::logger_t log;
    MDBX_env *env;
    std::string db_dir;
    model::cluster_ptr_t cluster;
};

} // namespace net
} // namespace syncspirit
