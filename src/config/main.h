// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2026 Ivan Baidakou

#pragma once
#include <cstdint>
#include "acceptor.h"
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
#include "utils/path.h"

namespace syncspirit::config {

struct main_t {
    utils::path_t config_path;
    utils::path_t default_location;
    std::string ssl_verify_store;
    utils::path_t cert_file;
    utils::path_t key_file;

    acceptor_config_t acceptor_config;
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
    std::uint32_t poll_timeout; // in microseconds

    main_t() noexcept = default;

    inline main_t(const main_t &orig) noexcept { *this = orig; }

    inline main_t &operator=(const main_t &orig) noexcept {
        config_path = orig.config_path.clone();
        default_location = orig.default_location.clone();
        ssl_verify_store = orig.ssl_verify_store;
        cert_file = orig.cert_file.clone();
        key_file = orig.key_file.clone();

        acceptor_config = orig.acceptor_config;
        local_announce_config = orig.local_announce_config;
        log_configs = orig.log_configs;
        upnp_config = orig.upnp_config;
        global_announce_config = orig.global_announce_config;
        bep_config = orig.bep_config;
        dialer_config = orig.dialer_config;
        fs_config = orig.fs_config;
        db_config = orig.db_config;
        relay_config = orig.relay_config;
        fltk_config = orig.fltk_config;

        timeout = orig.timeout;
        device_name = orig.device_name;
        hasher_threads = orig.hasher_threads;
        poll_timeout = orig.poll_timeout;

        return *this;
    }
};

} // namespace syncspirit::config
