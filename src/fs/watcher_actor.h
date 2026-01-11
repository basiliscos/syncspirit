// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#pragma once

#include "syncspirit-export.h"
#include "syncspirit-config.h"
#include "fs_context.h"
#include "utils/log.h"
#include <rotor.hpp>
#include <filesystem>
#include <boost/system.hpp>

namespace syncspirit::fs {

namespace r = rotor;
namespace bfs = std::filesystem;
namespace sys = boost::system;

namespace payload {

struct watch_folder_t {
    bfs::path path;
    sys::error_code ec;
};

} // namespace payload

namespace message {
using watch_folder_t = r::message_t<payload::watch_folder_t>;
}

struct SYNCSPIRIT_API watch_actor_t : r::actor_base_t {
    using parent_t = r::actor_base_t;
    using config_t = parent_t::config_t;

    explicit watch_actor_t(config_t &cfg);
    void do_initialize(r::system_context_t *ctx) noexcept override;
    void configure(r::plugin::plugin_base_t &plugin) noexcept override;
    void shutdown_finish() noexcept override;
    void on_watch(message::watch_folder_t &) noexcept;
    void inotify_callback() noexcept;

#if SYNCSPIRIT_WATCHER_INOTIFY
    int inotify_fd = -1;
    fs_context_t::io_guard_t root_guard;
#endif
    utils::logger_t log;
};

} // namespace syncspirit::fs
