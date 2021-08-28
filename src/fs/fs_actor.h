#pragma once

#include "../config/main.h"
#include "../utils/log.h"
#include "messages.h"
#include "continuation.h"
#include <rotor.hpp>
#include <deque>

namespace syncspirit {
namespace fs {

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
    using error_ptr_t = r::extended_error_ptr_t;

    void on_scan_request(message::scan_request_t &req) noexcept;
    void on_scan_cancel(message::scan_cancel_t &req) noexcept;

    void on_open(message::open_request_t &req) noexcept;
    void on_close(message::close_request_t &req) noexcept;
    void on_scan(message::scan_t &req) noexcept;
    void on_process(message::process_signal_t &) noexcept;
    void scan_dir(bfs::path &dir, payload::scan_t &payload) noexcept;
    void process_queue() noexcept;
    void reply(message::scan_t &req) noexcept;

    uint32_t calc_block(payload::scan_t &payload) noexcept;

    error_ptr_t check_digest(const std::string &data, const std::string &hash, const bfs::path &path) noexcept;

    utils::logger_t log;
    requests_t queue;
    bool scan_cancelled = false;
    r::address_ptr_t coordinator;
    config::fs_config_t fs_config;
};

} // namespace fs
} // namespace syncspirit
