// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include <functional>
#include "config/main.h"
#include "utils/log.h"
#include "model/messages.h"
#include "model/misc/sequencer.h"
#include "syncspirit-export.h"
#include <rotor/thread.hpp>

namespace syncspirit {
namespace fs {

namespace r = rotor;
namespace rth = rotor::thread;

struct SYNCSPIRIT_API fs_supervisor_config_t : r::supervisor_config_t {
    config::fs_config_t fs_config;
    uint32_t hasher_threads;
    model::sequencer_ptr_t sequencer;
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
    builder_t &&sequencer(model::sequencer_ptr_t value) && noexcept {
        parent_t::config.sequencer = std::move(value);
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
};

struct SYNCSPIRIT_API fs_supervisor_t : rth::supervisor_thread_t {
    using launcher_t = std::function<void(model::cluster_ptr_t &)>;
    using parent_t = rth::supervisor_thread_t;
    using config_t = fs_supervisor_config_t;
    template <typename Supervisor> using config_builder_t = fs_supervisor_config_builder_t<Supervisor>;

    explicit fs_supervisor_t(config_t &cfg);
    void configure(r::plugin::plugin_base_t &plugin) noexcept override;
    void on_start() noexcept override;
    template <typename F> void add_launcher(F &&launcher) noexcept { launchers.push_back(std::forward<F>(launcher)); }

  private:
    using model_request_ptr_t = r::intrusive_ptr_t<model::message::model_request_t>;
    using launchers_t = std::vector<launcher_t>;

    void on_model_request(model::message::model_request_t &req) noexcept;
    void on_model_response(model::message::model_response_t &res) noexcept;
    void on_model_update(model::message::model_update_t &message) noexcept;
    void on_block_update(model::message::block_update_t &message) noexcept;
    void launch() noexcept;

    model::sequencer_ptr_t sequencer;
    model::cluster_ptr_t cluster;
    utils::logger_t log;
    config::fs_config_t fs_config;
    uint32_t hasher_threads;
    r::address_ptr_t coordinator;
    r::actor_ptr_t scan_actor;
    r::actor_ptr_t file_actor;
    model_request_ptr_t model_request;
    launchers_t launchers;
};

} // namespace fs
} // namespace syncspirit
