#pragma once

#include "messages.h"
#include <unordered_set>

namespace syncspirit {
namespace net {

struct controller_actor_config_t : r::actor_config_t {
    model::cluster_ptr_t cluster;
    model::device_ptr_t device;
    model::device_ptr_t peer;
    r::address_ptr_t peer_addr;
};

template <typename Actor> struct controller_actor_config_builder_t : r::actor_config_builder_t<Actor> {
    using builder_t = typename Actor::template config_builder_t<Actor>;
    using parent_t = r::actor_config_builder_t<Actor>;
    using parent_t::parent_t;

    builder_t &&cluster(const model::cluster_ptr_t &value) &&noexcept {
        parent_t::config.cluster = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&device(const model::device_ptr_t &value) &&noexcept {
        parent_t::config.device = value;
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
};

enum class sync_state_t { none, syncing, paused };

struct controller_actor_t : public r::actor_base_t {
    using config_t = controller_actor_config_t;
    template <typename Actor> using config_builder_t = controller_actor_config_builder_t<Actor>;

    controller_actor_t(config_t &config);
    void configure(r::plugin::plugin_base_t &plugin) noexcept override;
    void on_start() noexcept override;
    void shutdown_start() noexcept override;
    bool on_unlink(const r::address_ptr_t &peer_addr) noexcept override;

  private:
    using peers_map_t = std::unordered_map<r::address_ptr_t, model::device_ptr_t>;
    void on_forward(message::forwarded_message_t &message) noexcept;
    void on_store_folder_info(message::store_folder_info_response_t &message) noexcept;

    void on_message(proto::message::Index &message) noexcept;
    void on_message(proto::message::IndexUpdate &message) noexcept;
    void on_message(proto::message::Request &message) noexcept;
    void on_message(proto::message::Response &message) noexcept;
    void on_message(proto::message::DownloadProgress &message) noexcept;

    model::cluster_ptr_t cluster;
    model::folder_ptr_t folder;
    model::device_ptr_t device;
    model::device_ptr_t peer;
    r::address_ptr_t peer_addr;
    r::address_ptr_t db;
    sync_state_t sync_state;
    peers_map_t peers_map;
};

} // namespace net
} // namespace syncspirit
