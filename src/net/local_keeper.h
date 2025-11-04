// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include "fs/messages.h"
#include "hasher/messages.h"
#include "model/messages.h"
#include "model/cluster.h"
#include "model/diff/cluster_visitor.h"
#include "model/misc/sequencer.h"

namespace syncspirit::net {

namespace outcome = boost::outcome_v2;
namespace r = rotor;

struct local_keeper_config_t : r::actor_config_t {
    model::cluster_ptr_t cluster;
    model::sequencer_ptr_t sequencer;
    uint32_t concurrent_hashes;
};

template <typename Actor> struct local_keeper_config_builder_t : r::actor_config_builder_t<Actor> {
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
    builder_t &&concurrent_hashes(uint32_t value) && noexcept {
        parent_t::config.concurrent_hashes = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
};

struct SYNCSPIRIT_API local_keeper_t final : public r::actor_base_t, private model::diff::cluster_visitor_t {
    using config_t = local_keeper_config_t;
    template <typename Actor> using config_builder_t = local_keeper_config_builder_t<Actor>;

    explicit local_keeper_t(config_t &cfg);

    void on_start() noexcept override;
    void shutdown_start() noexcept override;
    void configure(r::plugin::plugin_base_t &plugin) noexcept override;

    void on_model_update(model::message::model_update_t &) noexcept;
    void on_post_process(fs::message::foreign_executor_t &) noexcept;
    void on_digest(hasher::message::digest_t &res) noexcept;

    outcome::result<void> operator()(const model::diff::local::scan_start_t &, void *custom) noexcept override;

    utils::logger_t log;
    model::sequencer_ptr_t sequencer;
    model::cluster_ptr_t cluster;
    r::address_ptr_t coordinator;
    r::address_ptr_t fs_addr;
    std::int32_t concurrent_hashes_left;
    std::int32_t concurrent_hashes_limit;
    std::int32_t fs_tasks = 0;
};

} // namespace syncspirit::net
