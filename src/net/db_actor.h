#pragma once

#include "messages.h"
#include "mdbx.h"
#include "../utils/log.h"
#include "../db/transaction.h"
#include <optional>
#include <random>

namespace syncspirit {
namespace net {

namespace outcome = boost::outcome_v2;

struct db_actor_config_t : r::actor_config_t {
    std::string db_dir;
    model::device_ptr_t device;
};

template <typename Actor> struct db_actor_config_builder_t : r::actor_config_builder_t<Actor> {
    using builder_t = typename Actor::template config_builder_t<Actor>;
    using parent_t = r::actor_config_builder_t<Actor>;
    using parent_t::parent_t;

    builder_t &&db_dir(const std::string &value) &&noexcept {
        parent_t::config.db_dir = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&device(const model::device_ptr_t &value) &&noexcept {
        parent_t::config.device = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
};

struct db_actor_t : public r::actor_base_t {
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
    void on_store_ignored_device(message::store_ignored_device_request_t &message) noexcept;
    void on_store_device(message::store_device_request_t &message) noexcept;
    void on_store_folder_info(message::store_folder_info_request_t &message) noexcept;
    void on_store_ignored_folder(message::store_ignored_folder_request_t &message) noexcept;
    void on_store_new_folder(message::store_new_folder_request_t &message) noexcept;
    void on_store_folder(message::store_folder_request_t &message) noexcept;
    void on_store_file(message::store_file_request_t &message) noexcept;

    void open() noexcept;
    outcome::result<void> save(db::transaction_t &txn, model::folder_info_ptr_t &folder_info) noexcept;

    utils::logger_t log;
    std::random_device rd;
    std::uniform_int_distribution<std::int64_t> distribution;
    std::mt19937 generator;
    MDBX_env *env;
    std::string db_dir;
    model::device_ptr_t device;
};

} // namespace net
} // namespace syncspirit
