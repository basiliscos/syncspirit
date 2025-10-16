// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include <functional>
#include "config/fs.h"
#include "model/messages.h"
#include "model/diff/iterative_controller.h"
#include "model/misc/sequencer.h"
#include "syncspirit-export.h"
#include "file_cache.h"
#include <rotor/thread.hpp>

namespace syncspirit {
namespace fs {

namespace r = rotor;
namespace rth = rotor::thread;
namespace outcome = boost::outcome_v2;

struct SYNCSPIRIT_API fs_supervisor_config_t : r::supervisor_config_t {
    config::fs_config_t fs_config;
    uint32_t hasher_threads;
    model::sequencer_ptr_t sequencer;
    r::address_ptr_t bouncer_address;
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
    builder_t &&bouncer_address(const r::address_ptr_t &value) && noexcept {
        parent_t::config.bouncer_address = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
};

template <typename T> using fs_base_t = model::diff::iterative_controller_t<T, rth::supervisor_thread_t>;

struct SYNCSPIRIT_API fs_supervisor_t : fs_base_t<fs_supervisor_t> {
    using parent_t = fs_base_t<fs_supervisor_t>;
    using launcher_t = std::function<void(model::cluster_ptr_t &)>;
    using config_t = fs_supervisor_config_t;
    template <typename Supervisor> using config_builder_t = fs_supervisor_config_builder_t<Supervisor>;

    explicit fs_supervisor_t(config_t &cfg);
    void configure(r::plugin::plugin_base_t &plugin) noexcept override;
    void on_start() noexcept override;
    void on_child_shutdown(actor_base_t *actor) noexcept override;
    template <typename F> void add_launcher(F &&launcher) noexcept { launchers.push_back(std::forward<F>(launcher)); }

  private:
    using model_request_ptr_t = r::intrusive_ptr_t<model::message::model_request_t>;
    using launchers_t = std::vector<launcher_t>;

    void on_model_request(model::message::model_request_t &req) noexcept;
    void on_model_response(model::message::model_response_t &res) noexcept;
    void on_db_loaded(model::message::db_loaded_t &) noexcept;
    void commit_loading() noexcept override;
    void launch() noexcept;

    outcome::result<void> apply(const model::diff::advance::advance_t &, void *) noexcept override;
    outcome::result<void> apply(const model::diff::modify::upsert_folder_t &, void *) noexcept override;
    outcome::result<void> apply(const model::diff::modify::upsert_folder_info_t &, void *) noexcept override;
    outcome::result<void> apply(const model::diff::peer::update_folder_t &, void *) noexcept override;

    model::sequencer_ptr_t sequencer;
    config::fs_config_t fs_config;
    uint32_t hasher_threads;
    r::actor_ptr_t scan_actor;
    r::actor_ptr_t file_actor;
    model_request_ptr_t model_request;
    launchers_t launchers;
    file_cache_ptr_t rw_cache;
};

} // namespace fs
} // namespace syncspirit
