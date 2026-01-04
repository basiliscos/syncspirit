// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2026 Ivan Baidakou

#pragma once

#include "config/fs.h"
#include "syncspirit-export.h"
#include "file_cache.h"
#include "utils/log.h"
#include <rotor/thread.hpp>

namespace syncspirit {
namespace fs {

namespace r = rotor;
namespace rth = rotor::thread;
namespace outcome = boost::outcome_v2;

struct SYNCSPIRIT_API fs_supervisor_config_t : r::supervisor_config_t {
    config::fs_config_t fs_config;
    uint32_t hasher_threads;
};

template <typename Supervisor> struct fs_supervisor_config_builder_t : r::supervisor_config_builder_t<Supervisor> {
    using builder_t = typename Supervisor::template config_builder_t<Supervisor>;
    using parent_t = r::supervisor_config_builder_t<Supervisor>;
    using parent_t::parent_t;

    builder_t &&fs_config(const config::fs_config_t &value) && noexcept {
        parent_t::config.fs_config = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
    builder_t &&hasher_threads(uint32_t value) && noexcept {
        parent_t::config.hasher_threads = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
};

struct SYNCSPIRIT_API fs_supervisor_t : rth::supervisor_thread_t {
    using parent_t = rth::supervisor_thread_t;
    using config_t = fs_supervisor_config_t;
    template <typename Supervisor> using config_builder_t = fs_supervisor_config_builder_t<Supervisor>;

    explicit fs_supervisor_t(config_t &cfg);
    void configure(r::plugin::plugin_base_t &plugin) noexcept override;
    void enqueue(r::message_ptr_t message) noexcept override;
    void on_start() noexcept override;
    void on_child_shutdown(actor_base_t *actor) noexcept override;

  protected:
    virtual void launch_children() noexcept;

  private:
    utils::logger_t log;
    config::fs_config_t fs_config;
    uint32_t hasher_threads;
    file_cache_ptr_t rw_cache;
};

} // namespace fs
} // namespace syncspirit
