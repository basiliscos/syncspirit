#pragma once

#include "messages.h"
#include "mdbx.h"
#include <optional>
#include <random>

namespace syncspirit {
namespace net {

struct folder_actor_config_t : r::actor_config_t {
    model::folder_ptr_t folder;
    model::device_ptr_t device;
};

template <typename Actor> struct folder_actor_config_builder_t : r::actor_config_builder_t<Actor> {
    using builder_t = typename Actor::template config_builder_t<Actor>;
    using parent_t = r::actor_config_builder_t<Actor>;
    using parent_t::parent_t;

    builder_t &&folder(const model::folder_ptr_t &value) &&noexcept {
        parent_t::config.folder = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&device(const model::device_ptr_t &value) &&noexcept {
        parent_t::config.device = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
};

enum class sync_state_t { none, syncing, paused };

struct folder_actor_t : public r::actor_base_t {
    using config_t = folder_actor_config_t;
    template <typename Actor> using config_builder_t = folder_actor_config_builder_t<Actor>;

    folder_actor_t(config_t &config);
    void configure(r::plugin::plugin_base_t &plugin) noexcept override;
    void on_start() noexcept override;
    void shutdown_start() noexcept override;

  private:
    void on_start_sync(message::start_sync_t &message) noexcept;
    void on_stop_sync(message::stop_sync_t &message) noexcept;

    model::folder_ptr_t folder;
    model::device_ptr_t device;
    r::address_ptr_t db;
    sync_state_t sync_state;
};

} // namespace net
} // namespace syncspirit
