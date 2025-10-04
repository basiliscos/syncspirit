// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include "file_cache.h"
#include "model/cluster.h"
#include "model/messages.h"
#include "model/misc/sequencer.h"
#include "model/diff/cluster_visitor.h"
#include "utils/log.h"
#include "hasher/messages.h"
#include "messages.h"
#include "scan_task.h"
#include <rotor.hpp>

namespace syncspirit {
namespace fs {

namespace r = rotor;
namespace outcome = boost::outcome_v2;

struct SYNCSPIRIT_API scan_actor_config_t : r::actor_config_t {
    config::fs_config_t fs_config;
    model::sequencer_ptr_t sequencer;
    file_cache_ptr_t rw_cache;
    uint32_t requested_hashes_limit;
};

template <typename Actor> struct scan_actor_config_builder_t : r::actor_config_builder_t<Actor> {
    using builder_t = typename Actor::template config_builder_t<Actor>;
    using parent_t = r::actor_config_builder_t<Actor>;
    using parent_t::parent_t;

    builder_t &&fs_config(const config::fs_config_t &value) && noexcept {
        parent_t::config.fs_config = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&requested_hashes_limit(uint32_t value) && noexcept {
        parent_t::config.requested_hashes_limit = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&sequencer(model::sequencer_ptr_t value) && noexcept {
        parent_t::config.sequencer = std::move(value);
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&rw_cache(file_cache_ptr_t value) && noexcept {
        parent_t::config.rw_cache = std::move(value);
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
};

struct SYNCSPIRIT_API scan_actor_t : public r::actor_base_t, private model::diff::cluster_visitor_t {
    using config_t = scan_actor_config_t;
    template <typename Actor> using config_builder_t = scan_actor_config_builder_t<Actor>;

    explicit scan_actor_t(config_t &cfg);

    void on_start() noexcept override;
    void configure(r::plugin::plugin_base_t &plugin) noexcept override;

  private:
    using clock_t = r::pt::microsec_clock;

    scan_errors_t initiate_hash(scan_task_ptr_t task, const bfs::path &path, proto::FileInfo &metadata) noexcept;
    void commit_new_file(new_chunk_iterator_t &info) noexcept;

    void on_thread_ready(model::message::thread_ready_t &) noexcept;
    void on_model_update(model::message::model_update_t &message) noexcept;
    void on_scan(message::scan_progress_t &message) noexcept;
    void on_hash(hasher::message::digest_response_t &res) noexcept;
    void on_rehash(message::rehash_needed_t &message) noexcept;
    void on_hash_anew(message::hash_anew_t &message) noexcept;
    void on_hash_new(hasher::message::digest_response_t &res) noexcept;
    void post_scan(scan_task_t &task) noexcept;

    outcome::result<void> operator()(const model::diff::local::scan_start_t &, void *custom) noexcept override;

    template <typename Message> void hash_next(Message &m, const r::address_ptr_t &reply_addr) noexcept;

    model::cluster_ptr_t cluster;
    model::sequencer_ptr_t sequencer;
    config::fs_config_t fs_config;
    file_cache_ptr_t rw_cache;
    r::address_ptr_t hasher_proxy;
    r::address_ptr_t new_files; /* for routing */
    utils::logger_t log;
    r::address_ptr_t coordinator;
    uint32_t requested_hashes_limit;
    uint32_t requested_hashes = 0;
    uint32_t progress = 0;
};

} // namespace fs
} // namespace syncspirit
