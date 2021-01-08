#pragma once

#include "messages.h"
#include "mdbx.h"
#include <optional>
#include <random>

namespace syncspirit {
namespace net {

struct db_actor_config_t : r::actor_config_t {
    std::string db_dir;
    model::device_id_t device_id;
};

template <typename Actor> struct db_actor_config_builder_t : r::actor_config_builder_t<Actor> {
    using builder_t = typename Actor::template config_builder_t<Actor>;
    using parent_t = r::actor_config_builder_t<Actor>;
    using parent_t::parent_t;

    builder_t &&db_dir(const std::string &value) &&noexcept {
        parent_t::config.db_dir = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&device_id(const model::device_id_t &value) &&noexcept {
        parent_t::config.device_id = value;
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

  private:
    void on_make_index_id(message::make_index_id_request_t &message) noexcept;
    void open() noexcept;
    std::random_device rd;
    std::uniform_int_distribution<model::index_id_t> distribution;
    MDBX_env *env;
    std::string db_dir;
    model::device_id_t device_id;
};

} // namespace net
} // namespace syncspirit
