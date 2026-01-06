// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2026 Ivan Baidakou

#pragma once

#include "file_cache.h"
#include "messages.h"
#include "net/messages.h"
#include "hasher/hasher_plugin.h"
#include "utils/log.h"
#include "model/file_info.h"
#include <rotor.hpp>

namespace syncspirit::fs {

namespace r = rotor;
namespace outcome = boost::outcome_v2;

struct SYNCSPIRIT_API file_actor_config_t : r::actor_config_t {
    file_cache_ptr_t rw_cache;
    uint32_t concurrent_hashes;
};

template <typename Actor> struct file_actor_config_builder_t : r::actor_config_builder_t<Actor> {
    using builder_t = typename Actor::template config_builder_t<Actor>;
    using parent_t = r::actor_config_builder_t<Actor>;
    using parent_t::parent_t;

    builder_t &&rw_cache(file_cache_ptr_t value) && noexcept {
        parent_t::config.rw_cache = std::move(value);
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
    builder_t &&concurrent_hashes(uint32_t value) && noexcept {
        parent_t::config.concurrent_hashes = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
};

struct SYNCSPIRIT_API file_actor_t : public r::actor_base_t {
    template <typename Actor> using config_builder_t = file_actor_config_builder_t<Actor>;
    using config_t = file_actor_config_t;
    using plugins_list_t =
        std::tuple<r::plugin::address_maker_plugin_t, r::plugin::lifetime_plugin_t, r::plugin::init_shutdown_plugin_t,
                   r::plugin::link_server_plugin_t, r::plugin::link_client_plugin_t, hasher::hasher_plugin_t,
                   r::plugin::resources_plugin_t, r::plugin::starter_plugin_t>;

    explicit file_actor_t(config_t &cfg);

    void on_start() noexcept override;
    void shutdown_start() noexcept override;
    void shutdown_finish() noexcept override;
    void configure(r::plugin::plugin_base_t &plugin) noexcept override;

  private:
    void on_exec(message::foreign_executor_t &) noexcept;
    void on_io_commands(message::io_commands_t &) noexcept;
    void on_create_dir(message::create_dir_t &) noexcept;
    void process(payload::block_request_t &) noexcept;
    void process(payload::remote_copy_t &) noexcept;
    void process(payload::append_block_t &) noexcept;
    void process(payload::finish_file_t &) noexcept;
    void process(payload::clone_block_t &) noexcept;

    void on_controller_up(net::message::controller_up_t &message) noexcept;
    void on_controller_predown(net::message::controller_predown_t &message) noexcept;

    outcome::result<file_ptr_t> get_source_for_cloning(model::file_info_ptr_t &source,
                                                       const model::folder_info_t &source_fi,
                                                       const file_ptr_t &target_backend) noexcept;

    outcome::result<file_ptr_t> open_file_rw(const bfs::path &path, std::uint64_t file_size) noexcept;
    outcome::result<file_ptr_t> open_file_ro(const bfs::path &path, bool use_cache = false) noexcept;

    utils::logger_t log;
    r::address_ptr_t coordinator;
    r::address_ptr_t db;
    file_cache_ptr_t rw_cache;
    uint32_t concurrent_hashes;
    hasher::hasher_plugin_t *hasher = nullptr;
};

} // namespace syncspirit::fs
