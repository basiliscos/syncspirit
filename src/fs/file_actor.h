// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#pragma once

#include "file.h"
#include "model/cluster.h"
#include "model/messages.h"
#include "model/diff/block_visitor.h"
#include "model/misc/lru_cache.hpp"
#include "config/main.h"
#include "utils/log.h"
#include "utils.h"
#include <rotor.hpp>

namespace syncspirit {

namespace model::details {

template <> inline std::string_view get_lru_key<fs::file_ptr_t>(const fs::file_ptr_t &item) {
    return item->get_path_view();
}

} // namespace model::details

namespace fs {

namespace r = rotor;
namespace outcome = boost::outcome_v2;

struct SYNCSPIRIT_API file_actor_config_t : r::actor_config_t {
    model::cluster_ptr_t cluster;
    size_t mru_size;
};

template <typename Actor> struct file_actor_config_builder_t : r::actor_config_builder_t<Actor> {
    using builder_t = typename Actor::template config_builder_t<Actor>;
    using parent_t = r::actor_config_builder_t<Actor>;
    using parent_t::parent_t;

    builder_t &&cluster(const model::cluster_ptr_t &value) &&noexcept {
        parent_t::config.cluster = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&mru_size(size_t value) &&noexcept {
        parent_t::config.mru_size = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
};

struct SYNCSPIRIT_API file_actor_t : public r::actor_base_t,
                                     private model::diff::block_visitor_t,
                                     private model::diff::cluster_visitor_t {
    using config_t = file_actor_config_t;
    template <typename Actor> using config_builder_t = file_actor_config_builder_t<Actor>;

    explicit file_actor_t(config_t &cfg);

    void on_start() noexcept override;
    void shutdown_start() noexcept override;
    void configure(r::plugin::plugin_base_t &plugin) noexcept override;

  private:
    using cache_t = model::mru_list_t<file_ptr_t>;

    void on_model_update(model::message::model_update_t &message) noexcept;
    void on_block_update(model::message::block_update_t &message) noexcept;

    outcome::result<file_ptr_t> open_file_rw(const bfs::path &path, model::file_info_ptr_t info) noexcept;
    outcome::result<file_ptr_t> open_file_ro(const bfs::path &path) noexcept;

    outcome::result<void> operator()(const model::diff::modify::clone_file_t &) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::flush_file_t &) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::append_block_t &) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::clone_block_t &) noexcept override;

    outcome::result<void> reflect(model::file_info_ptr_t &file) noexcept;

    model::cluster_ptr_t cluster;
    utils::logger_t log;
    r::address_ptr_t coordinator;
    cache_t files_cache;
};

} // namespace fs
} // namespace syncspirit
