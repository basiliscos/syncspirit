// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "utils.h"

#include <boost/asio/ip/host_name.hpp>
#include <boost/tokenizer.hpp>
#include <boost/regex.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/nowide/convert.hpp>
#include <spdlog/spdlog.h>
#include "utils/log.h"
#include "utils/location.h"

#define TOML_EXCEPTIONS 0
#include <toml++/toml.h>

#define SAFE_GET_VALUE(property, type, table_name)                                                                     \
    {                                                                                                                  \
        auto option = t[#property].value<type>();                                                                      \
        if (!option) {                                                                                                 \
            spdlog::warn("using default value for {}/{}", table_name, #property);                                      \
            c.property = c_default.property;                                                                           \
        } else {                                                                                                       \
            c.property = option.value();                                                                               \
        }                                                                                                              \
    }

#define SAFE_GET_PATH(property, table_name)                                                                            \
    {                                                                                                                  \
        auto option = t[#property].value<std::string>();                                                               \
        if (!option) {                                                                                                 \
            spdlog::warn("using default value for {}/{}", table_name, #property);                                      \
            c.property = c_default.property;                                                                           \
        } else {                                                                                                       \
            c.property = boost::nowide::widen(option.value());                                                         \
        }                                                                                                              \
    }

#define SAFE_GET_PATH_OPTIONAL(property, table_name)                                                                   \
    {                                                                                                                  \
        auto option = t[#property].value<std::string>();                                                               \
        if (option) {                                                                                                  \
            c.property = boost::nowide::widen(option.value());                                                         \
        }                                                                                                              \
    }

#define SAFE_GET_PATH_EXPANDED(property, table_name)                                                                   \
    {                                                                                                                  \
        auto option = t[#property].value<std::string>();                                                               \
        if (!option) {                                                                                                 \
            spdlog::warn("using default value for {}/{}", table_name, #property);                                      \
            c.property = c_default.property;                                                                           \
        } else {                                                                                                       \
            c.property = utils::expand_home(option.value(), home_opt);                                                 \
        }                                                                                                              \
    }

#define SAFE_GET_URL(property, table_name)                                                                             \
    {                                                                                                                  \
        auto option = t[#property].value<std::string>();                                                               \
        if (option) {                                                                                                  \
            auto url = utils::parse(option.value().c_str());                                                           \
            if (url) {                                                                                                 \
                c.property = url;                                                                                      \
            }                                                                                                          \
        }                                                                                                              \
        if (!c.property) {                                                                                             \
            spdlog::warn("using default value for {}/{}", table_name, #property);                                      \
            c.property = c_default.property;                                                                           \
        }                                                                                                              \
    }

#define SAFE_GET_LEVEL(property, table_name)                                                                           \
    {                                                                                                                  \
        auto option = t[#property].value<std::string>();                                                               \
        if (!option) {                                                                                                 \
            spdlog::warn("using default value for {}/{}", table_name, #property);                                      \
            c.property = c_default.property;                                                                           \
        } else {                                                                                                       \
            c.property = utils::get_log_level(option.value()).value_or(level_t::debug);                                \
        }                                                                                                              \
    }

//        c.level = utils::get_log_level(level.value()).value_or(level_t::debug);

namespace sys = boost::system;

#if defined(__unix__)
static const std::string home_path = "~/.config/syncspirit";
#else
static const std::string home_path = "~/syncspirit";
#endif

namespace syncspirit::config {

using level_t = spdlog::level::level_enum;

using home_option_t = outcome::result<bfs::path>;

static std::string get_device_name() noexcept {
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
    wchar_t device_name[MAX_COMPUTERNAME_LENGTH + 1] = {0};
    DWORD device_name_sz = sizeof(device_name) / sizeof(wchar_t);
    if (GetComputerNameW(device_name, &device_name_sz)) {
        return boost::nowide::narrow(device_name, static_cast<size_t>(device_name_sz));
#else
    sys::error_code ec;
    auto device_name = boost::asio::ip::host_name(ec);
    if (!ec) {
        return device_name;
#endif
    } else {
        return "localhost";
    }
}

static main_t make_default_config(const bfs::path &config_path, const bfs::path &config_dir, bool is_home) {
    auto dir = config_path;
    std::string cert_file = home_path + "/cert.pem";
    std::string key_file = home_path + "/key.pem";
    if (!is_home) {
        using boost::algorithm::replace_all_copy;
        cert_file = replace_all_copy(cert_file, home_path, dir.string());
        key_file = replace_all_copy(key_file, home_path, dir.string());
    }

    auto device = get_device_name();

    // clang-format off
    main_t cfg;
    cfg.config_path = config_path;
    cfg.default_location = config_dir / L"shared-data";
    cfg.root_ca_file = bfs::path{};
    cfg.cert_file = cert_file;
    cfg.key_file = key_file;
    cfg.timeout = 30000;
    cfg.device_name = device;
    cfg.hasher_threads = 3;
    cfg.log_configs = {
        // log_config_t {
        //     "default", spdlog::level::level_enum::trace, {"stdout"}
        // }
    };
    cfg.local_announce_config = local_announce_config_t {
        true,   /* enabled */
        21027,  /* port */
        30000   /* frequency */
    };
    cfg.global_announce_config = global_announce_config_t{
        true,                                                           /* enabled */
        false,                                                          /* debug */
        utils::parse("https://discovery-announce-v4.syncthing.net/v2"), /* announce_url */
        utils::parse("https://discovery-lookup.syncthing.net/v2"),      /* lookup_url */
        32 * 1024,                                                      /* rx_buff_size */
        3000,                                                           /* timeout */
        10 * 60,                                                        /* reannounce timeout */
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
        32,                 /* blocks_max_requested */
        64,                 /* blocks_simultaneous_write */
        20,                 /* advances_per_iteration */
        500,                /* stats_interval */
    };
    cfg.dialer_config = dialer_config_t {
        true,       /* enabled */
        5 * 60000,  /* redial timeout */
        10          /* skip_discovers */
    };
    cfg.fs_config = fs_config_t {
        86400000,   /* temporally_timeout, 24h default */
        128,        /* mru_size max number of open files for reading and writing */
        1024*1024,  /* bytes_scan_iteration_limit max number of bytes before emitting scan events */
        128,        /* files_scan_iteration_limit max number processed files before emitting scan events */
    };
    cfg.db_config = db_config_t {
        0x0,           /* upper_limit, auto-adjust */
        150,           /* uncommitted_threshold */
        50*1024,       /* max blocks per diff */
        5*1024,        /* max files per diff */
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
        true,                               /* display_missing */
        true,                               /* display_colorized */
        700,                                /* main_window_width */
        480,                                /* main_window_height */
        0.5,                                /* left_panel_share */
        0.3,                                /* bottom_panel_share */
        99'999,                             /* log_records_buffer */
    };
    return cfg;
}

config_result_t get_config(std::istream &config, const bfs::path &config_path) {
    auto dir = config_path.parent_path();
    main_t cfg;
    cfg.config_path = config_path;

    auto home_opt = utils::get_home_dir();
    auto r = toml::parse(config);
    if (!r) {
        return std::string(r.error().description());
    }

    auto config_dir_opt = utils::get_default_config_dir();
    if (!config_dir_opt) {
        auto ec = config_dir_opt.assume_error();
        return fmt::format("cannot get config dir: {}", ec.message());
    }
    auto &config_dir = config_dir_opt.assume_value();
    bool is_home = dir == config_dir;
    auto default_config = make_default_config(config_path, dir, is_home);

    auto &root_tbl = r.table();
    // main
    {
        auto t = root_tbl["main"];
        auto &c = cfg;
        auto &c_default = default_config;

        SAFE_GET_VALUE(timeout, std::uint32_t, "main");
        SAFE_GET_VALUE(device_name, std::string, "main");
        SAFE_GET_PATH(default_location, "main");
        SAFE_GET_VALUE(hasher_threads, std::uint32_t, "main");
        SAFE_GET_PATH_OPTIONAL(root_ca_file, "main");
        SAFE_GET_PATH_EXPANDED(cert_file, "main");
        SAFE_GET_PATH_EXPANDED(key_file, "main");
    };

    // local_discovery
    {
        auto t = root_tbl["local_discovery"];
        auto &c = cfg.local_announce_config;
        auto &c_default = default_config.local_announce_config;

        SAFE_GET_VALUE(enabled, bool, "local_discovery");
        SAFE_GET_VALUE(port, std::uint16_t, "local_discovery");
        SAFE_GET_VALUE(frequency, std::uint32_t, "local_discovery");
    }

    // global_discovery
    {
        auto t = root_tbl["global_discovery"];
        auto &c = cfg.global_announce_config;
        auto &c_default = default_config.global_announce_config;

        SAFE_GET_VALUE(enabled, bool, "global_discovery");
        SAFE_GET_VALUE(debug, bool, "global_discovery");
        SAFE_GET_URL(announce_url, "global_discovery");
        SAFE_GET_URL(lookup_url, "global_discovery");
        SAFE_GET_VALUE(rx_buff_size, std::uint32_t, "global_discovery");
        SAFE_GET_VALUE(timeout, std::uint32_t, "timeout");
    };

    // upnp
    {
        auto t = root_tbl["upnp"];
        auto &c = cfg.upnp_config;
        auto &c_default = default_config.upnp_config;

        SAFE_GET_VALUE(enabled, bool, "upnp");
        SAFE_GET_VALUE(debug, bool, "upnp");
        SAFE_GET_VALUE(max_wait, std::uint32_t, "upnp");
        SAFE_GET_VALUE(external_port, std::uint32_t, "upnp");
        SAFE_GET_VALUE(rx_buff_size, std::uint32_t, "upnp");
    };

    // relay
    {
        auto t = root_tbl["relay"];
        auto &c = cfg.relay_config;
        auto &c_default = default_config.relay_config;

        SAFE_GET_VALUE(enabled, bool, "relay");
        SAFE_GET_VALUE(debug, bool, "relay");
        SAFE_GET_URL(discovery_url, "relay");
        SAFE_GET_VALUE(rx_buff_size, std::uint32_t, "relay");
    };

    // bep
    {
        auto t = root_tbl["bep"];
        auto &c = cfg.bep_config;
        auto &c_default = default_config.bep_config;

        SAFE_GET_VALUE(rx_buff_size, std::uint32_t, "bep");
        SAFE_GET_VALUE(tx_buff_limit, std::uint32_t, "bep");
        SAFE_GET_VALUE(connect_timeout, std::uint32_t, "bep");
        SAFE_GET_VALUE(request_timeout, std::uint32_t, "bep");
        SAFE_GET_VALUE(tx_timeout, std::uint32_t, "bep");
        SAFE_GET_VALUE(rx_timeout, std::uint32_t, "bep");
        SAFE_GET_VALUE(blocks_max_requested, std::uint32_t, "bep");
        SAFE_GET_VALUE(blocks_simultaneous_write, std::uint32_t, "bep");
        SAFE_GET_VALUE(advances_per_iteration, std::uint32_t, "bep");
        SAFE_GET_VALUE(stats_interval, std::int32_t, "bep");
    }

    // dialer
    {
        auto t = root_tbl["dialer"];
        auto &c = cfg.dialer_config;
        auto &c_default = default_config.dialer_config;

        SAFE_GET_VALUE(enabled, bool, "dialer");
        SAFE_GET_VALUE(redial_timeout, std::uint32_t, "dialer");
        SAFE_GET_VALUE(skip_discovers, std::uint32_t, "skip_discovers");
    }

    // fs
    {
        auto t = root_tbl["fs"];
        auto &c = cfg.fs_config;
        auto &c_default = default_config.fs_config;

        SAFE_GET_VALUE(temporally_timeout, std::uint32_t, "fs");
        SAFE_GET_VALUE(mru_size, std::uint32_t, "fs");
        SAFE_GET_VALUE(bytes_scan_iteration_limit, std::int64_t, "fs");
        SAFE_GET_VALUE(files_scan_iteration_limit, std::int64_t, "fs");
    }

    // db
    {
        auto t = root_tbl["db"];
        auto &c = cfg.db_config;
        auto &c_default = default_config.db_config;

        SAFE_GET_VALUE(upper_limit, std::int64_t, "db");
        SAFE_GET_VALUE(uncommitted_threshold, std::uint32_t, "db");
        SAFE_GET_VALUE(max_blocks_per_diff, std::uint32_t, "db");
        SAFE_GET_VALUE(max_files_per_diff, std::uint32_t, "db");
    }

    // fltk
    {
        auto t = root_tbl["fltk"];
        auto &c = cfg.fltk_config;
        auto &c_default = default_config.fltk_config;

        SAFE_GET_LEVEL(level, "fltk");
        SAFE_GET_VALUE(display_deleted, bool, "fltk");
        SAFE_GET_VALUE(display_missing, bool, "fltk");
        SAFE_GET_VALUE(display_colorized, bool, "fltk");
        SAFE_GET_VALUE(main_window_width, std::int64_t, "fltk");
        SAFE_GET_VALUE(main_window_height, std::int64_t, "fltk");
        SAFE_GET_VALUE(left_panel_share, double, "fltk");
        SAFE_GET_VALUE(bottom_panel_share, double, "fltk");
        SAFE_GET_VALUE(log_records_buffer, std::uint32_t, "fltk");
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
    using boost::nowide::narrow;

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

    auto cert_file = cfg.cert_file;
    cert_file.make_preferred();

    auto key_file = cfg.key_file;
    key_file.make_preferred();

    auto tbl = toml::table{{
        {"main", toml::table{{
                     {"hasher_threads", cfg.hasher_threads},
                     {"root_ca_file", narrow(cfg.root_ca_file.wstring())},
                     {"cert_file", narrow(cert_file.wstring())},
                     {"key_file", narrow(key_file.wstring())},
                     {"timeout", cfg.timeout},
                     {"device_name", cfg.device_name},
                     {"default_location", narrow(cfg.default_location.wstring())},
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
                                 {"lookup_url", cfg.global_announce_config.lookup_url->buffer().data()},
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
                    {"advances_per_iteration", cfg.bep_config.advances_per_iteration},
                    {"blocks_max_requested", cfg.bep_config.blocks_max_requested},
                    {"blocks_simultaneous_write", cfg.bep_config.blocks_simultaneous_write},
                    {"connect_timeout", cfg.bep_config.connect_timeout},
                    {"request_timeout", cfg.bep_config.request_timeout},
                    {"rx_buff_size", cfg.bep_config.rx_buff_size},
                    {"rx_timeout", cfg.bep_config.rx_timeout},
                    {"stats_interval", cfg.bep_config.stats_interval},
                    {"tx_buff_limit", cfg.bep_config.tx_buff_limit},
                    {"tx_timeout", cfg.bep_config.tx_timeout},
                }}},
        {"dialer", toml::table{{
                       {"enabled", cfg.dialer_config.enabled},
                       {"redial_timeout", cfg.dialer_config.redial_timeout},
                       {"skip_discovers", cfg.dialer_config.skip_discovers},
                   }}},
        {"fs", toml::table{{
                   {"temporally_timeout", cfg.fs_config.temporally_timeout},
                   {"mru_size", cfg.fs_config.mru_size},
                   {"bytes_scan_iteration_limit", cfg.fs_config.bytes_scan_iteration_limit},
                   {"files_scan_iteration_limit", cfg.fs_config.files_scan_iteration_limit},
               }}},
        {"db", toml::table{{
                   {"upper_limit", cfg.db_config.upper_limit},
                   {"uncommitted_threshold", cfg.db_config.uncommitted_threshold},
                   {"max_blocks_per_diff", cfg.db_config.max_blocks_per_diff},
                   {"max_files_per_diff", cfg.db_config.max_files_per_diff},
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
                     {"display_missing", cfg.fltk_config.display_missing},
                     {"display_colorized", cfg.fltk_config.display_colorized},
                     {"main_window_width", cfg.fltk_config.main_window_width},
                     {"main_window_height", cfg.fltk_config.main_window_height},
                     {"left_panel_share", cfg.fltk_config.left_panel_share},
                     {"bottom_panel_share", cfg.fltk_config.bottom_panel_share},
                     {"log_records_buffer", cfg.fltk_config.log_records_buffer},
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
    return make_default_config(config_path, config_dir, is_home);
}

} // namespace syncspirit::config
