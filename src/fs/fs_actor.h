#pragma once

#include "../config/main.h"
#include "messages.h"
#include "continuation.h"
#include <rotor/thread.hpp>
#include <deque>

namespace syncspirit {
namespace fs {

namespace rth = rotor::thread;

struct fs_actor_config_t : r::actor_config_t {
    config::fs_config_t fs_config;
};

template <typename Actor> struct fs_actor_config_builder_t : r::actor_config_builder_t<Actor> {
    using builder_t = typename Actor::template config_builder_t<Actor>;
    using parent_t = r::actor_config_builder_t<Actor>;
    using parent_t::parent_t;

    builder_t &&fs_config(const config::fs_config_t &value) &&noexcept {
        parent_t::config.fs_config = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
};

struct fs_actor_t : public r::actor_base_t {
    using config_t = fs_actor_config_t;
    template <typename Actor> using config_builder_t = fs_actor_config_builder_t<Actor>;

    explicit fs_actor_t(config_t &cfg);

    void on_start() noexcept override;
    void shutdown_finish() noexcept override;
    void configure(r::plugin::plugin_base_t &plugin) noexcept override;

  private:
    using requests_t = std::deque<request_ptr_t>;

    void on_scan_request(message::scan_request_t &req) noexcept;
    void on_scan(message::scan_t &req) noexcept;
    void scan_dir(bfs::path &dir, payload::scan_t &payload) noexcept;
    void process_queue() noexcept;
    void reply(message::scan_t &req) noexcept;
    uint32_t calc_block(payload::scan_t &payload) noexcept;

    requests_t queue;
    r::address_ptr_t coordinator;
    config::fs_config_t fs_config;
};

} // namespace fs
} // namespace syncspirit
