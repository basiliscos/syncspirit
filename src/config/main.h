// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once
#include <cstdint>
#include "bep.h"
#include "db.h"
#include "dialer.h"
#include "fs.h"
#include "global_announce.h"
#include "local_announce.h"
#include "log.h"
#include "relay.h"
#include "upnp.h"
#include "fltk.h"
#include <boost/filesystem.hpp>

namespace syncspirit::config {

namespace bfs = boost::filesystem;

struct main_t {
    bfs::path config_path;
    bfs::path default_location;

    local_announce_config_t local_announce_config;
    log_configs_t log_configs;
    upnp_config_t upnp_config;
    global_announce_config_t global_announce_config;
    bep_config_t bep_config;
    dialer_config_t dialer_config;
    fs_config_t fs_config;
    db_config_t db_config;
    relay_config_t relay_config;
    fltk_config_t fltk_config;

    std::uint32_t timeout;
    std::string device_name;
    std::uint32_t hasher_threads;
};

} // namespace syncspirit::config
