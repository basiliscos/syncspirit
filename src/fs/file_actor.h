// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2026 Ivan Baidakou

#pragma once

#include "messages.h"
#include "file.h"
#include "updates_mediator.h"
#include "watched_folders.h"
#include "net/messages.h"
#include "hasher/hasher_plugin.h"
#include "utils/log.h"
#include "model/file_info.h"
#include "model/messages.h"
#include <rotor.hpp>
#include <optional>

namespace syncspirit::fs {

namespace r = rotor;
namespace outcome = boost::outcome_v2;

struct SYNCSPIRIT_API file_actor_config_t : r::actor_config_t {
    using scan_dir_callback_t = execution_context_t::scan_dir_callback_t;
    uint32_t concurrent_hashes;
    r::pt::time_duration change_retension;
    updates_mediator_ptr_t updates_mediator;
    watched_folders_ptr_t watched_folders;
    scan_dir_callback_t scan_dir_callback;
};

template <typename Actor> struct file_actor_config_builder_t : r::actor_config_builder_t<Actor> {
    using builder_t = typename Actor::template config_builder_t<Actor>;
    using parent_t = r::actor_config_builder_t<Actor>;
    using parent_t::parent_t;

    builder_t &&concurrent_hashes(uint32_t value) && noexcept {
        parent_t::config.concurrent_hashes = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
    builder_t &&change_retension(const r::pt::time_duration &value) && noexcept {
        parent_t::config.change_retension = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
    builder_t &&updates_mediator(updates_mediator_ptr_t value) && noexcept {
        parent_t::config.updates_mediator = std::move(value);
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
    builder_t &&watched_folders(watched_folders_ptr_t value) && noexcept {
        parent_t::config.watched_folders = std::move(value);
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
    builder_t &&scan_dir_callback(execution_context_t::scan_dir_callback_t value) && noexcept {
        parent_t::config.scan_dir_callback = std::move(value);
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
    struct process_context_t;

    explicit file_actor_t(config_t &cfg);

    void on_start() noexcept override;
    void shutdown_start() noexcept override;
    void shutdown_finish() noexcept override;
    void configure(r::plugin::plugin_base_t &plugin) noexcept override;
    template <typename T> auto &access() noexcept;

  private:
    using clock_t = pt::microsec_clock;
    using file_cache_t = std::unordered_map<utils::path_t, file_ptr_t, utils::path_hash_t, utils::path_eq_t>;
    using context_cache_t = std::unordered_map<const void *, file_cache_t>;
    using timer_opt_t = std::optional<r::request_id_t>;
    using scan_dir_callback_t = execution_context_t::scan_dir_callback_t;
    using io_commands_ptr_t = r::intrusive_ptr_t<message::io_commands_t>;
    using io_queue_t = std::list<io_commands_ptr_t>;
    using io_signal_ptr_t = r::intrusive_ptr_t<message::io_signal_t>;

    void on_exec(message::foreign_executor_t &) noexcept;
    void on_io_commands(message::io_commands_t &) noexcept;
    void on_io_signal(message::io_signal_t &) noexcept;
    void on_create_dir(message::create_dir_t &) noexcept;
    void process(payload::block_request_t &, process_context_t &) noexcept;
    void process(payload::remote_copy_t &, process_context_t &) noexcept;
    void process(payload::append_block_t &, process_context_t &) noexcept;
    void process(payload::finish_file_t &, process_context_t &) noexcept;
    void process(payload::clone_block_t &, process_context_t &) noexcept;
    void process(payload::update_meta_t &, process_context_t &) noexcept;

    void on_controller_up(net::message::controller_up_t &message) noexcept;
    void on_controller_predown(net::message::controller_predown_t &message) noexcept;
    void on_service_lock(model::message::service_lock_t &message) noexcept;
    void on_service_unlock(model::message::service_unlock_t &message) noexcept;

    void on_retension_finish(r::request_id_t, bool cancelled) noexcept;

    outcome::result<file_ptr_t> get_source_for_cloning(model::file_info_ptr_t &source,
                                                       const model::folder_info_t &source_fi,
                                                       const file_ptr_t &target_backend) noexcept;

    outcome::result<file_ptr_t> open_file_rw(const utils::poly_path_view_t &path, std::uint64_t file_size,
                                             process_context_t &) noexcept;
    outcome::result<file_ptr_t> open_file_ro(const utils::poly_path_view_t &path, const void *context = {}) noexcept;

    utils::logger_t log;
    uint32_t concurrent_hashes;
    r::pt::time_duration retension;
    updates_mediator_ptr_t updates_mediator;
    watched_folders_ptr_t watched_folders;
    r::address_ptr_t coordinator;
    r::address_ptr_t db;
    context_cache_t context_cache;
    hasher::hasher_plugin_t *hasher = nullptr;
    timer_opt_t expiration_timer;
    scan_dir_callback_t scan_dir_callback;
    io_queue_t io_queue;
    io_signal_ptr_t io_signal;
};

} // namespace syncspirit::fs
