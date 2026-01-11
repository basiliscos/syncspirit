// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#pragma once

#include "syncspirit-export.h"

#include <rotor.hpp>
#include <filesystem>
#include <boost/system.hpp>
#include "utils/log.h"

namespace syncspirit::fs::platform {

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

struct SYNCSPIRIT_API watcher_base_t : r::actor_base_t {
    using parent_t = r::actor_base_t;
    using config_t = parent_t::config_t;

    explicit watcher_base_t(config_t &cfg);
    void configure(r::plugin::plugin_base_t &plugin) noexcept override;
    virtual void on_watch(message::watch_folder_t &) noexcept;

    utils::logger_t log;
};


} // namespace syncspirit::fs::platform


