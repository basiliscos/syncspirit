// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#pragma once

#include "syncspirit-export.h"

#include <rotor.hpp>
#include <filesystem>
#include <boost/system.hpp>
#include <string>
#include <optional>
#include <unordered_map>
#include "utils/log.h"
#include "proto/proto-fwd.hpp"
#include "fs/update_type.hpp"
#include "fs/updates_mediator.h"
#include "fs/updates_support.h"

namespace syncspirit::fs::platform {

namespace r = rotor;
namespace bfs = std::filesystem;
namespace sys = boost::system;

namespace payload {

struct watch_folder_t {
    bfs::path path;
    std::string folder_id;
    sys::error_code ec;
};

struct file_info_t : proto::FileInfo {
    using parent_t = proto::FileInfo;
    inline file_info_t(proto::FileInfo file_info, update_type_t update_reason_)
        : parent_t(std::move(file_info)), update_reason{update_reason_} {};
    update_type_t update_reason;
};

using file_changes_t = std::vector<file_info_t>;

struct folder_change_t {
    std::string folder_id;
    file_changes_t file_changes;
};
using folder_changes_t = std::vector<folder_change_t>;

} // namespace payload

namespace message {
using watch_folder_t = r::message_t<payload::watch_folder_t>;
using folder_changes_t = r::message_t<payload::folder_changes_t>;
} // namespace message

struct SYNCSPIRIT_API watcher_config_t : r::actor_config_t {
    r::pt::time_duration change_retension;
    updates_mediator_ptr_t updates_mediator;
};

template <typename Actor> struct watcher_config_builder_t : r::actor_config_builder_t<Actor> {
    using builder_t = typename Actor::template config_builder_t<Actor>;
    using parent_t = r::actor_config_builder_t<Actor>;
    using parent_t::parent_t;

    builder_t &&change_retension(const r::pt::time_duration &value) && noexcept {
        parent_t::config.change_retension = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
    builder_t &&updates_mediator(updates_mediator_ptr_t value) && noexcept {
        parent_t::config.updates_mediator = std::move(value);
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
};

struct SYNCSPIRIT_API watcher_base_t : r::actor_base_t {
    using parent_t = r::actor_base_t;
    template <typename Actor> using config_builder_t = watcher_config_builder_t<Actor>;
    using config_t = watcher_config_t;

    struct folder_info_t {
        bfs::path path;
        std::string path_str;
    };

    using folder_map_t = std::unordered_map<std::string, folder_info_t>;

    struct folder_update_t {
        std::string folder_id;
        support::file_updates_t updates;

        void update(std::string_view relative_path, update_type_t type, folder_update_t *prev) noexcept;
        auto make(const folder_info_t &folder_info, updates_mediator_t &mediator) noexcept -> payload::file_changes_t;
    };
    using folder_updates_t = std::vector<folder_update_t>;
    using clock_t = r::pt::microsec_clock;
    using timepoint_t = r::pt::ptime;
    using interval_t = r::pt::time_duration;
    using folder_changes_opt_t = std::optional<payload::folder_changes_t>;
    struct bulk_update_t {
        timepoint_t deadline;
        folder_updates_t updates;
        folder_update_t &prepare(std::string_view folder_id) noexcept;
        folder_update_t *find(std::string_view folder_id) noexcept;
        folder_changes_opt_t make(const folder_map_t &folder_map, updates_mediator_t &mediator) noexcept;
        bool has_changes() const noexcept;
    };

    explicit watcher_base_t(config_t &cfg);
    void configure(r::plugin::plugin_base_t &plugin) noexcept override;
    virtual void on_watch(message::watch_folder_t &) noexcept;
    void on_retension_finish(r::request_id_t, bool cancelled) noexcept;
    void push(const timepoint_t &deadline, std::string_view folder_id, std::string_view relative_path,
              update_type_t type) noexcept;

    utils::logger_t log;
    interval_t retension;
    updates_mediator_ptr_t updates_mediator;
    r::address_ptr_t coordinator;
    folder_map_t folder_map;
    bulk_update_t next;
    bulk_update_t postponed;
};

} // namespace syncspirit::fs::platform
