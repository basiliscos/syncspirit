// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include "file_cache.h"
#include "messages.h"
#include "net/messages.h"
#include "model/cluster.h"
#include "model/messages.h"
#include "model/diff/cluster_visitor.h"
#include "model/misc/sequencer.h"
#include "utils/log.h"
#include <rotor.hpp>

namespace syncspirit::fs {

namespace r = rotor;
namespace outcome = boost::outcome_v2;

struct SYNCSPIRIT_API file_actor_config_t : r::actor_config_t {
    model::cluster_ptr_t cluster;
    model::sequencer_ptr_t sequencer;
    file_cache_ptr_t rw_cache;
};

template <typename Actor> struct file_actor_config_builder_t : r::actor_config_builder_t<Actor> {
    using builder_t = typename Actor::template config_builder_t<Actor>;
    using parent_t = r::actor_config_builder_t<Actor>;
    using parent_t::parent_t;

    builder_t &&cluster(const model::cluster_ptr_t &value) && noexcept {
        parent_t::config.cluster = value;
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

struct SYNCSPIRIT_API file_actor_t : public r::actor_base_t, private model::diff::cluster_visitor_t {
    using config_t = file_actor_config_t;
    template <typename Actor> using config_builder_t = file_actor_config_builder_t<Actor>;

    explicit file_actor_t(config_t &cfg);

    void on_start() noexcept override;
    void shutdown_start() noexcept override;
    void shutdown_finish() noexcept override;
    void configure(r::plugin::plugin_base_t &plugin) noexcept override;

  private:
    struct write_guard_t {
        write_guard_t(file_actor_t &actor, const model::diff::modify::block_transaction_t &txn) noexcept;
        ~write_guard_t();

        outcome::result<void> operator()(outcome::result<void> result) noexcept;

        file_actor_t &actor;
        const model::diff::modify::block_transaction_t &txn;
        bool success;
    };

    void on_model_update(model::message::model_update_t &message) noexcept;
    void on_block_request(message::block_request_t &message) noexcept;
    void on_controller_up(net::message::controller_up_t &message) noexcept;
    void on_controller_predown(net::message::controller_predown_t &message) noexcept;

    outcome::result<file_ptr_t> get_source_for_cloning(model::file_info_ptr_t &source,
                                                       const file_ptr_t &target_backend) noexcept;

    outcome::result<file_ptr_t> open_file_rw(const bfs::path &path, model::file_info_ptr_t info) noexcept;
    outcome::result<file_ptr_t> open_file_ro(const bfs::path &path, bool use_cache = false) noexcept;

    outcome::result<void> operator()(const model::diff::advance::remote_copy_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::advance::remote_win_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::finish_file_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::append_block_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::clone_block_t &, void *) noexcept override;

    outcome::result<void> reflect(model::file_info_ptr_t &file, const bfs::path &path) noexcept;

    model::cluster_ptr_t cluster;
    model::sequencer_ptr_t sequencer;
    utils::logger_t log;
    r::address_ptr_t coordinator;
    r::address_ptr_t db;
    file_cache_ptr_t rw_cache;
    file_cache_t ro_cache;
};

} // namespace syncspirit::fs
