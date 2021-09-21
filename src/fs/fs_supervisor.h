#pragma once

#include "../config/main.h"
#include "../utils/log.h"
#include <rotor/thread.hpp>

namespace syncspirit {
namespace fs {

namespace r = rotor;
namespace rth = rotor::thread;

struct fs_supervisor_config_t : r::supervisor_config_t {
    config::fs_config_t fs_config;
    uint32_t hasher_threads;
};

template <typename Supervisor> struct fs_supervisor_config_builder_t : r::supervisor_config_builder_t<Supervisor> {
    using builder_t = typename Supervisor::template config_builder_t<Supervisor>;
    using parent_t = r::supervisor_config_builder_t<Supervisor>;
    using parent_t::parent_t;

    builder_t &&fs_config(const config::fs_config_t &value) &&noexcept {
        parent_t::config.fs_config = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
    builder_t &&hasher_threads(uint32_t value) &&noexcept {
        parent_t::config.hasher_threads = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
};

struct fs_supervisor_t : rth::supervisor_thread_t {
    using parent_t = rth::supervisor_thread_t;
    using config_t = fs_supervisor_config_t;
    template <typename Supervisor> using config_builder_t = fs_supervisor_config_builder_t<Supervisor>;

    explicit fs_supervisor_t(config_t &cfg);
    void configure(r::plugin::plugin_base_t &plugin) noexcept override;
    void on_start() noexcept override;

  private:
    void launch() noexcept;

    utils::logger_t log;
    config::fs_config_t fs_config;
    uint32_t hasher_threads;
    r::address_ptr_t coordinator;
    r::actor_ptr_t fs_actor;
};

} // namespace fs
} // namespace syncspirit
