#pragma once

#include "model/cluster.h"
#include "model/messages.h"
#include "config/main.h"
#include "utils/log.h"
#include "hasher/messages.h"
#include "messages.h"
#include "scan_task.h"
#include <rotor.hpp>
#include <deque>

namespace syncspirit {
namespace fs {

namespace r = rotor;
namespace outcome = boost::outcome_v2;

struct scan_actor_config_t : r::actor_config_t {
    config::fs_config_t fs_config;
    model::cluster_ptr_t cluster;
    r::address_ptr_t hasher_proxy;
    uint32_t requested_hashes_limit;
};

template <typename Actor> struct scan_actor_config_builder_t : r::actor_config_builder_t<Actor> {
    using builder_t = typename Actor::template config_builder_t<Actor>;
    using parent_t = r::actor_config_builder_t<Actor>;
    using parent_t::parent_t;

    builder_t &&fs_config(const config::fs_config_t &value) &&noexcept {
        parent_t::config.fs_config = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&hasher_proxy(r::address_ptr_t &value) &&noexcept {
        parent_t::config.hasher_proxy = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&requested_hashes_limit(uint32_t value) &&noexcept {
        parent_t::config.requested_hashes_limit = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&cluster(const model::cluster_ptr_t &value) &&noexcept {
        parent_t::config.cluster = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
};

struct scan_actor_t : public r::actor_base_t {
    using config_t = scan_actor_config_t;
    template <typename Actor> using config_builder_t = scan_actor_config_builder_t<Actor>;

    explicit scan_actor_t(config_t &cfg);

    void on_start() noexcept override;
    void shutdown_finish() noexcept override;
    void configure(r::plugin::plugin_base_t &plugin) noexcept override;

  private:
    using scan_request_t = r::intrusive_ptr_t<message::scan_folder_t>;
    using scan_queue_t = std::list<scan_request_t>;

    void initiate_scan(std::string_view folder_id) noexcept;
    model::io_errors_t initiate_rehash(scan_task_ptr_t task, model::file_info_ptr_t file) noexcept;
    bool rehash_next(message::rehash_needed_t &message) noexcept;
    void process_queue() noexcept;

    void on_model_update(model::message::model_update_t &message) noexcept;
    void on_initiate_scan(message::scan_folder_t& message) noexcept;
    void on_scan(message::scan_progress_t& message) noexcept;
    void on_hash(hasher::message::digest_response_t &res) noexcept;
    void on_rehash(message::rehash_needed_t& message) noexcept;

    model::cluster_ptr_t cluster;
    utils::logger_t log;
    r::address_ptr_t hasher_proxy;
    r::address_ptr_t coordinator;
    config::fs_config_t fs_config;
    uint32_t requested_hashes_limit;
    uint32_t requested_hashes = 0;
    std::uint32_t generation = 0;
    scan_queue_t queue;
};

} // namespace fs
} // namespace syncspirit
