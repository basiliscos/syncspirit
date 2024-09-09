// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "utils.h"

#include <boost/asio/ip/host_name.hpp>
#include <boost/tokenizer.hpp>
#include <boost/regex.hpp>
#include <boost/algorithm/string.hpp>
#include <spdlog/spdlog.h>
#include "utils/log.h"
#include "utils/location.h"

#define TOML_EXCEPTIONS 0
#include <toml++/toml.h>

namespace sys = boost::system;

#if defined(__unix__)
static const std::string home_path = "~/.config/syncspirit";
#else
static const std::string home_path = "~/syncspirit";
#endif

namespace syncspirit::config {

using device_name_t = outcome::result<std::string>;
using home_option_t = outcome::result<bfs::path>;

static device_name_t get_device_name() noexcept {
    sys::error_code ec;
    auto device_name = boost::asio::ip::host_name(ec);
    if (ec) {
        return ec;
    }
    return device_name;
}

config_result_t get_config(std::istream &config, const bfs::path &config_path) {
    main_t cfg;
    cfg.config_path = config_path;

    auto home_opt = utils::get_home_dir();
    auto r = toml::parse(config);
    if (!r) {
        return std::string(r.error().description());
    }

    auto &root_tbl = r.table();
    // main
    {
        auto t = root_tbl["main"];
        auto &c = cfg;
        auto timeout = t["timeout"].value<std::uint32_t>();
        if (!timeout) {
            return "main/timeout is incorrect or missing";
        }
        c.timeout = timeout.value();

        auto device_name = t["device_name"].value<std::string>();
        if (!device_name) {
            auto option = get_device_name();
            if (!option)
                return option.error().message();
            device_name = option.value();
        }
        c.device_name = device_name.value();

        auto default_location = t["default_location"].value<std::string>();
        if (!default_location) {
            return "main/default_location is incorrect or missing";
        }
        c.default_location = default_location.value();

        auto hasher_threads = t["hasher_threads"].value<std::uint32_t>();
        if (!hasher_threads) {
            return "main/hasher_threads is incorrect or missing";
        }
        c.hasher_threads = hasher_threads.value();
    };

    // log
    {
        auto t = root_tbl["log"];
        auto &c = cfg.log_configs;
        if (t.is_array_of_tables()) {
            auto arr = t.as_array();
            for (size_t i = 0; i < arr->size(); ++i) {
                auto node = arr->get(i);
                if (node->is_table()) {
                    auto t = *node->as_table();
                    auto level = t["level"].value<std::string>();
                    if (!level) {
                        return "log/level is incorrect or missing (" + std::to_string(i + 1) + ")";
                    }
                    auto name = t["name"].value<std::string>();
                    if (!name) {
                        return "log/name is incorrect or missing (" + std::to_string(i + 1) + ")";
                    }
                    log_config_t log_config;
                    log_config.level = utils::get_log_level(level.value());
                    log_config.name = name.value();

                    auto sinks = t["sinks"];
                    if (sinks) {
                        auto s_arr = sinks.as_array();
                        for (size_t j = 0; j < s_arr->size(); ++j) {
                            auto sink = s_arr->get(j)->value<std::string>();
                            if (!sink) {
                                return "log/sinks " + std::to_string(j + 1) + " is incorrect or missing (" +
                                       std::to_string(i + 1) + ")";
                            }
                            log_config.sinks.emplace_back(sink.value());
                        }
                    }
                    c.emplace_back(std::move(log_config));
                }
            }
        }
    }

    // local_discovery
    {
        auto t = root_tbl["local_discovery"];
        auto &c = cfg.local_announce_config;

        auto enabled = t["enabled"].value<bool>();
        if (!enabled) {
            return "local_discovery/enabled is incorrect or missing";
        }
        c.enabled = enabled.value();

        auto port = t["port"].value<std::uint16_t>();
        if (!port) {
            return "local_discovery/port is incorrect or missing";
        }
        c.port = port.value();

        auto frequency = t["frequency"].value<std::uint32_t>();
        if (!frequency) {
            return "local_discovery/frequency is incorrect or missing";
        }
        c.frequency = frequency.value();
    }

    // global_discovery
    {
        auto t = root_tbl["global_discovery"];
        auto &c = cfg.global_announce_config;

        auto enabled = t["enabled"].value<bool>();
        if (!enabled) {
            return "global_discovery/enabled is incorrect or missing";
        }
        c.enabled = enabled.value();

        auto debug = t["debug"].value<bool>();
        if (!debug) {
            return "global_discovery/debug is incorrect or missing";
        }
        c.debug = debug.value();

        auto url = t["announce_url"].value<std::string>();
        if (!url) {
            return "global_discovery/announce_url is incorrect or missing";
        }
        auto announce_url = utils::parse(url.value().c_str());
        if (!announce_url) {
            return "global_discovery/announce_url is not url";
        }
        c.announce_url = announce_url;

        auto device_id = t["device_id"].value<std::string>();
        if (!device_id) {
            return "global_discovery/device_id is incorrect or missing";
        }
        c.device_id = device_id.value();

        auto cert_file = t["cert_file"].value<std::string>();
        if (!cert_file) {
            return "global_discovery/cert_file is incorrect or missing";
        }
        c.cert_file = utils::expand_home(cert_file.value(), home_opt);

        auto key_file = t["key_file"].value<std::string>();
        if (!key_file) {
            return "global_discovery/key_file is incorrect or missing";
        }
        c.key_file = utils::expand_home(key_file.value(), home_opt);

        auto rx_buff_size = t["rx_buff_size"].value<std::uint32_t>();
        if (!rx_buff_size) {
            return "global_discovery/rx_buff_size is incorrect or missing";
        }
        c.rx_buff_size = rx_buff_size.value();

        auto timeout = t["timeout"].value<std::uint32_t>();
        if (!timeout) {
            return "global_discovery/timeout is incorrect or missing";
        }
        c.timeout = timeout.value();
    };

    // upnp
    {
        auto t = root_tbl["upnp"];
        auto &c = cfg.upnp_config;

        auto enabled = t["enabled"].value<bool>();
        if (!enabled) {
            return "upnp/enabled is incorrect or missing";
        }
        c.enabled = enabled.value();

        auto debug = t["debug"].value<bool>();
        if (!debug) {
            return "upnp/debug is incorrect or missing";
        }
        c.debug = debug.value();

        auto max_wait = t["max_wait"].value<std::uint32_t>();
        if (!max_wait) {
            return "upnp/max_wait is incorrect or missing";
        }
        c.max_wait = max_wait.value();

        auto external_port = t["external_port"].value<std::uint32_t>();
        if (!external_port) {
            return "upnp/external_port is incorrect or missing";
        }
        c.external_port = external_port.value();

        auto rx_buff_size = t["rx_buff_size"].value<std::uint32_t>();
        if (!rx_buff_size) {
            return "upnp/rx_buff_size is incorrect or missing";
        }
        c.rx_buff_size = rx_buff_size.value();
    };

    // relay
    {
        auto t = root_tbl["relay"];
        auto &c = cfg.relay_config;

        auto enabled = t["enabled"].value<bool>();
        if (!enabled) {
            return "relay/enabled is incorrect or missing";
        }
        c.enabled = enabled.value();

        auto debug = t["debug"].value<bool>();
        if (!debug) {
            return "relay/debug is incorrect or missing";
        }
        c.debug = debug.value();

        auto discovery_url_str = t["discovery_url"].value<std::string>();
        if (!discovery_url_str) {
            return "upnp/discovery_url is incorrect or missing";
        }
        auto discovery_url = utils::parse(discovery_url_str.value());
        if (!discovery_url_str) {
            return "upnp/discovery_url is non a valid url";
        }
        c.discovery_url = discovery_url;

        auto rx_buff_size = t["rx_buff_size"].value<std::uint32_t>();
        if (!rx_buff_size) {
            return "relay/rx_buff_size is incorrect or missing";
        }
        c.rx_buff_size = rx_buff_size.value();
    };

    // bep
    {
        auto t = root_tbl["bep"];
        auto &c = cfg.bep_config;
        auto rx_buff_size = t["rx_buff_size"].value<std::uint32_t>();
        if (!rx_buff_size) {
            return "bep/rx_buff_size is incorrect or missing";
        }
        c.rx_buff_size = rx_buff_size.value();

        auto tx_buff_limit = t["tx_buff_limit"].value<std::uint32_t>();
        if (!tx_buff_limit) {
            return "bep/tx_buff_limit is incorrect or missing";
        }
        c.tx_buff_limit = tx_buff_limit.value();

        auto connect_timeout = t["connect_timeout"].value<std::uint32_t>();
        if (!connect_timeout) {
            return "bep/connect_timeout is incorrect or missing";
        }
        c.connect_timeout = connect_timeout.value();

        auto request_timeout = t["request_timeout"].value<std::uint32_t>();
        if (!request_timeout) {
            return "bep/request_timeout is incorrect or missing";
        }
        c.request_timeout = request_timeout.value();

        auto tx_timeout = t["tx_timeout"].value<std::uint32_t>();
        if (!tx_timeout) {
            return "bep/tx_timeout is incorrect or missing";
        }
        c.tx_timeout = tx_timeout.value();

        auto rx_timeout = t["rx_timeout"].value<std::uint32_t>();
        if (!rx_timeout) {
            return "bep/rx_timeout is incorrect or missing";
        }
        c.rx_timeout = rx_timeout.value();

        auto blocks_max_requested = t["blocks_max_requested"].value<std::uint32_t>();
        if (!blocks_max_requested) {
            return "bep/blocks_max_requested is incorrect or missing";
        }
        c.blocks_max_requested = blocks_max_requested.value();

        auto blocks_simultaneous_write = t["blocks_simultaneous_write"].value<std::uint32_t>();
        if (!blocks_simultaneous_write) {
            return "bep/blocks_simultaneous_write is incorrect or missing";
        }
        c.blocks_simultaneous_write = blocks_simultaneous_write.value();
    }

    // dialer
    {
        auto t = root_tbl["dialer"];
        auto &c = cfg.dialer_config;

        auto enabled = t["enabled"].value<bool>();
        if (!enabled) {
            return "dialer/enabled is incorrect or missing";
        }
        c.enabled = enabled.value();

        auto redial_timeout = t["redial_timeout"].value<std::uint32_t>();
        if (!redial_timeout) {
            return "dialer/redial_timeout is incorrect or missing";
        }
        c.redial_timeout = redial_timeout.value();

        auto skip_discovers = t["skip_discovers"].value<std::uint32_t>();
        if (!skip_discovers) {
            return "dialer/skip_discovers is incorrect or missing";
        }
        c.skip_discovers = skip_discovers.value();
    }

    // fs
    {
        auto t = root_tbl["fs"];
        auto &c = cfg.fs_config;

        auto temporally_timeout = t["temporally_timeout"].value<std::uint32_t>();
        if (!temporally_timeout) {
            return "fs/temporally_timeout is incorrect or missing";
        }
        c.temporally_timeout = temporally_timeout.value();

        auto mru_size = t["mru_size"].value<std::uint32_t>();
        if (!mru_size) {
            return "fs/mru_size is incorrect or missing";
        }
        c.mru_size = mru_size.value();
    }

    // db
    {
        auto t = root_tbl["db"];
        auto &c = cfg.db_config;

        auto upper_limit = t["upper_limit"].value<std::int64_t>();
        if (!upper_limit) {
            return "db/upper_limit is incorrect or missing";
        }
        c.upper_limit = upper_limit.value();

        auto uncommitted_threshold = t["uncommitted_threshold"].value<std::uint32_t>();
        if (!uncommitted_threshold) {
            return "db/uncommitted_threshold is incorrect or missing";
        }
        c.uncommitted_threshold = uncommitted_threshold.value();
    }

    // fltk
    {
        auto t = root_tbl["fltk"];
        auto &c = cfg.fltk_config;

        auto level = t["level"].value<std::string>();
        if (!level) {
            return "fltk/level is incorrect or missing";
        }
        c.level = utils::get_log_level(level.value());

        auto display_deleted = t["display_deleted"].value<bool>();
        if (!display_deleted) {
            return "fltk/display_deleted is incorrect or missing";
        }
        c.display_deleted = display_deleted.value();

        auto main_window_width = t["main_window_width"].value<std::int64_t>();
        if (!main_window_width) {
            return "fltk/main_window_width is incorrect or missing";
        }
        c.main_window_width = main_window_width.value();

        auto main_window_height = t["main_window_height"].value<std::int64_t>();
        if (!main_window_height) {
            return "fltk/main_window_height is incorrect or missing";
        }
        c.main_window_height = main_window_height.value();

        auto left_panel_share = t["left_panel_share"].value<double>();
        if (!left_panel_share) {
            return "fltk/left_panel_share is incorrect or missing";
        }
        c.left_panel_share = left_panel_share.value();

        auto bottom_panel_share = t["bottom_panel_share"].value<double>();
        if (!bottom_panel_share) {
            return "fltk/bottom_panel_share is incorrect or missing";
        }
        c.bottom_panel_share = bottom_panel_share.value();
    }

    return cfg;
}

static std::string_view get_level(spdlog::level::level_enum level) noexcept {
    using L = spdlog::level::level_enum;
    switch (level) {
    case L::critical:
        return "critical";
    case L::debug:
        return "debug";
    case L::err:
        return "error";
    case L::info:
        return "info";
    case L::trace:
        return "trace";
    case L::warn:
        return "warn";
    case L::off:
        return "off";
    case L::n_levels:
        return "off";
    }
    return "unknown";
}

outcome::result<void> serialize(const main_t cfg, std::ostream &out) noexcept {
    auto logs = toml::array{};
    for (auto &c : cfg.log_configs) {
        auto sinks = toml::array{};
        for (auto &sink : c.sinks) {
            sinks.emplace_back<std::string>(sink);
        }
        auto log_table = toml::table{{
            {"name", c.name},
            {"level", get_level(c.level)},
            {"sinks", sinks},
        }};
        logs.push_back(log_table);
    }

    auto tbl = toml::table{{
        {"main", toml::table{{
                     {"hasher_threads", cfg.hasher_threads},
                     {"timeout", cfg.timeout},
                     {"device_name", cfg.device_name},
                     {"default_location", cfg.default_location.c_str()},
                 }}},
        {"log", logs},
        {"local_discovery", toml::table{{
                                {"enabled", cfg.local_announce_config.enabled},
                                {"port", cfg.local_announce_config.port},
                                {"frequency", cfg.local_announce_config.frequency},
                            }}},
        {"global_discovery", toml::table{{
                                 {"enabled", cfg.global_announce_config.enabled},
                                 {"debug", cfg.global_announce_config.debug},
                                 {"announce_url", cfg.global_announce_config.announce_url->buffer().data()},
                                 {"device_id", cfg.global_announce_config.device_id},
                                 {"cert_file", cfg.global_announce_config.cert_file},
                                 {"key_file", cfg.global_announce_config.key_file},
                                 {"rx_buff_size", cfg.global_announce_config.rx_buff_size},
                                 {"timeout", cfg.global_announce_config.timeout},
                             }}},
        {"upnp", toml::table{{
                     {"enabled", cfg.upnp_config.enabled},
                     {"debug", cfg.upnp_config.debug},
                     {"max_wait", cfg.upnp_config.max_wait},
                     {"external_port", cfg.upnp_config.external_port},
                     {"rx_buff_size", cfg.upnp_config.rx_buff_size},
                 }}},
        {"bep", toml::table{{
                    {"rx_buff_size", cfg.bep_config.rx_buff_size},
                    {"tx_buff_limit", cfg.bep_config.tx_buff_limit},
                    {"connect_timeout", cfg.bep_config.connect_timeout},
                    {"request_timeout", cfg.bep_config.request_timeout},
                    {"tx_timeout", cfg.bep_config.tx_timeout},
                    {"rx_timeout", cfg.bep_config.rx_timeout},
                    {"blocks_max_requested", cfg.bep_config.blocks_max_requested},
                    {"blocks_simultaneous_write", cfg.bep_config.blocks_simultaneous_write},
                }}},
        {"dialer", toml::table{{
                       {"enabled", cfg.dialer_config.enabled},
                       {"redial_timeout", cfg.dialer_config.redial_timeout},
                       {"skip_discovers", cfg.dialer_config.skip_discovers},
                   }}},
        {"fs", toml::table{{
                   {"temporally_timeout", cfg.fs_config.temporally_timeout},
                   {"mru_size", cfg.fs_config.mru_size},
               }}},
        {"db", toml::table{{
                   {"upper_limit", cfg.db_config.upper_limit},
                   {"uncommitted_threshold", cfg.db_config.uncommitted_threshold},
               }}},
        {"relay", toml::table{{
                      {"enabled", cfg.relay_config.enabled},
                      {"debug", cfg.relay_config.debug},
                      {"discovery_url", cfg.relay_config.discovery_url->buffer().data()},
                      {"rx_buff_size", cfg.relay_config.rx_buff_size},
                  }}},
        {"fltk", toml::table{{
                     {"level", get_level(cfg.fltk_config.level)},
                     {"display_deleted", cfg.fltk_config.display_deleted},
                     {"main_window_width", cfg.fltk_config.main_window_width},
                     {"main_window_height", cfg.fltk_config.main_window_height},
                     {"left_panel_share", cfg.fltk_config.left_panel_share},
                     {"bottom_panel_share", cfg.fltk_config.bottom_panel_share},
                 }}},
    }};
    // clang-format on
    out << tbl;
    return outcome::success();
}

outcome::result<main_t> generate_config(const bfs::path &config_path) {
    auto dir = config_path.parent_path();
    sys::error_code ec;
    bool exists = bfs::exists(dir, ec);
    if (!exists) {
        spdlog::info("creating directory {}", dir.string());
        bfs::create_directories(dir, ec);
        if (ec) {
            spdlog::error("cannot create dirs: {}", ec.message());
            return ec;
        }
    }

    std::string cert_file = home_path + "/cert.pem";
    std::string key_file = home_path + "/key.pem";
    auto config_dir_opt = utils::get_default_config_dir();
    if (!config_dir_opt) {
        auto ec = config_dir_opt.assume_error();
        auto msg = ec.message();
        spdlog::warn("cannot get config dir: {}", msg);
        return ec;
    }
    auto &config_dir = config_dir_opt.assume_value();
    bool is_home = dir == config_dir;
    if (!is_home) {
        using boost::algorithm::replace_all_copy;
        cert_file = replace_all_copy(cert_file, home_path, dir.string());
        key_file = replace_all_copy(key_file, home_path, dir.string());
    }

    auto device_name = get_device_name();
    auto device = std::string(device_name ? device_name.value() : "localhost");

    // clang-format off
    main_t cfg;
    cfg.config_path = config_path;
    cfg.default_location = config_dir / "shared_data";
    cfg.timeout = 5000;
    cfg.device_name = device;
    cfg.hasher_threads = 3;
    cfg.log_configs = {
        log_config_t {
            "default", spdlog::level::level_enum::info, {"stdout"}
        }
    };
    cfg.local_announce_config = local_announce_config_t {
        true,   /* enabled */
        21027,  /* port */
        30000   /* frequency */
    };
    cfg.global_announce_config = global_announce_config_t{
        true,                                                   /* enabled */
        false,                                                  /* debug */
        utils::parse("https://discovery.syncthing.net/"),
        "LYXKCHX-VI3NYZR-ALCJBHF-WMZYSPK-QG6QJA3-MPFYMSO-U56GTUK-NA2MIAW",
        cert_file,
        key_file,
        32 * 1024,
        3000,
        10 * 60,
    };
    cfg.upnp_config = upnp_config_t {
        true,       /* enabled */
        false,      /* debug */
        1,          /* max_wait */
        22001,      /* external port */
        64 * 1024   /* rx_buff */
    };
    cfg.bep_config = bep_config_t {
        16 * 1024 * 1024,   /* rx_buff_size */
        8 * 1024 * 1024,    /* tx_buff_limit */
        5000,               /* connect_timeout */
        60000,              /* request_timeout */
        90000,              /* tx_timeout */
        300000,             /* rx_timeout */
        16,                 /* blocks_max_requested */
        32,                 /* blocks_simultaneous_write */
    };
    cfg.dialer_config = dialer_config_t {
        true,       /* enabled */
        5 * 60000,  /* redial timeout */
        10          /* skip_discovers */
    };
    cfg.fs_config = fs_config_t {
        86400000,   /* temporally_timeout, 24h default */
        128,        /* mru_size max number of open files for reading and writing */
    };
    cfg.db_config = db_config_t {
        0x400000000,   /* upper_limit, 16Gb */
        150,           /* uncommitted_threshold */
    };

    cfg.relay_config = relay_config_t {
        true,                                                   /* enabled */
        false,                                                  /* debug */
        utils::parse("https://relays.syncthing.net/endpoint"),  /* discovery url */
        1024 * 1024,                                            /* rx buff size */
    };

    cfg.fltk_config = fltk_config_t {
        spdlog::level::level_enum::info,    /* level */
        false,                              /* display_deleted */
        700,                                /* main_window_width */
        480,                                /* main_window_height */
        0.5,                                /* left_panel_share */
        0.3,                                /* bottom_panel_share */
    };
    return cfg;
}

}
