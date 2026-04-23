// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025-2026 Ivan Baidakou

#pragma once

#include "fs/messages.h"
#include "hasher/messages.h"
#include "model_actor.hpp"
#include "model/messages.h"
#include "model/diff/cluster_visitor.h"
#include "model/misc/sequencer.h"
#include "utils/string_comparator.hpp"
#include "syncspirit-config.h"

namespace syncspirit::net {

namespace outcome = boost::outcome_v2;
namespace r = rotor;

namespace local_keeper {

struct folder_context_t;
using folder_context_ptr_t = boost::intrusive_ptr<folder_context_t>;

} // namespace local_keeper

struct SYNCSPIRIT_API local_keeper_t final : public model_actor_t<r::actor_base_t>,
                                             private model::diff::cluster_visitor_t {
    using parent_t = model_actor_t<r::actor_base_t>;

    struct config_t : parent_t::config_t {
        using base_t = model_actor_t<r::actor_base_t>::config_t;
        using base_t::base_t;
        model::sequencer_ptr_t sequencer;
        uint32_t concurrent_hashes;
        syncspirit_watcher_impl_t watcher_impl = syncspirit_watcher_impl_t::none;
    };

    template <typename Actor> struct config_builder_t : parent_t::template config_builder_t<Actor> {
        using builder_t = typename Actor::template config_builder_t<Actor>;
        using base_t = parent_t::template config_builder_t<Actor>;
        using base_t::base_t;

        builder_t &&sequencer(model::sequencer_ptr_t value) && noexcept {
            base_t::config.sequencer = std::move(value);
            return std::move(*static_cast<typename base_t::builder_t *>(this));
        }
        builder_t &&concurrent_hashes(uint32_t value) && noexcept {
            base_t::config.concurrent_hashes = value;
            return std::move(*static_cast<typename base_t::builder_t *>(this));
        }
        builder_t &&watcher_impl(syncspirit_watcher_impl_t value) && noexcept {
            base_t::config.watcher_impl = value;
            return std::move(*static_cast<typename base_t::builder_t *>(this));
        }
    };

    struct lc_context_t;
    using watched_folders_t = std::unordered_set<std::string, utils::string_hash_t, utils::string_eq_t>;
    using folder_contexts_t = std::list<local_keeper::folder_context_ptr_t>;

    explicit local_keeper_t(config_t &cfg);

    void on_start() noexcept override;
    void configure(r::plugin::plugin_base_t &plugin) noexcept override;
    void post_configure_coordinator() noexcept override;

    void visit(const model::diff::cluster_diff_t &, model::payload::apply_context_t &) noexcept override;
    void on_post_process(fs::message::foreign_executor_t &) noexcept;
    void on_digest(hasher::message::digest_t &res) noexcept;
    void on_thread_ready(model::message::thread_ready_t &) noexcept;
    void on_create_dir(fs::message::create_dir_t &) noexcept;
    void on_watch_dir(fs::message::watch_folder_t &) noexcept;
    void on_unwatch_dir(fs::message::unwatch_folder_t &) noexcept;
    void on_change(fs::message::folder_changes_t &) noexcept;
    void on_changes(model::folder_info_t &, fs::payload::file_changes_t &, lc_context_t &) noexcept;

    void handle_rename(fs::payload::file_info_t &change, const model::folder_info_t &local_folder,
                       lc_context_t &stack_ctx) noexcept;

    void try_start_watching() noexcept;

    outcome::result<void> operator()(const model::diff::local::scan_start_t &, void *custom) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::upsert_folder_t &, void *custom) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::remove_folder_t &, void *custom) noexcept override;

    using r::actor_base_t::make_error;
    model::sequencer_ptr_t sequencer;
    r::address_ptr_t fs_addr;
    r::address_ptr_t watcher_addr;
    syncspirit_watcher_impl_t watcher_impl;
    std::int32_t concurrent_hashes_left;
    std::int32_t concurrent_hashes_limit;
    std::int32_t fs_tasks = 0;
    folder_contexts_t delayed;
    bool started_watching = false;

    watched_folders_t watched_folders;
};

} // namespace syncspirit::net
