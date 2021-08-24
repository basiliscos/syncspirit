#include "utils.h"

#include <boost/asio/ip/host_name.hpp>
#include <boost/tokenizer.hpp>
#include <boost/regex.hpp>
#include <boost/algorithm/string.hpp>
#include <spdlog/spdlog.h>
#include "../model/device_id.h"
#include "../utils/log.h"

#define TOML_EXCEPTIONS 0
#include <toml++/toml.h>

namespace bfs = boost::filesystem;
namespace sys = boost::system;

static const std::string home_path = "~/.config/syncspirit";

namespace syncspirit::config {

bool operator==(const bep_config_t &lhs, const bep_config_t &rhs) noexcept {
    return lhs.rx_buff_size == rhs.rx_buff_size && lhs.connect_timeout == rhs.connect_timeout &&
           lhs.request_timeout == rhs.request_timeout && lhs.tx_timeout == rhs.tx_timeout &&
           lhs.rx_timeout == rhs.rx_timeout;
}

bool operator==(const dialer_config_t &lhs, const dialer_config_t &rhs) noexcept {
    return lhs.enabled == rhs.enabled && lhs.redial_timeout == rhs.redial_timeout;
}

bool operator==(const fs_config_t &lhs, const fs_config_t &rhs) noexcept {
    return lhs.batch_block_size == rhs.batch_block_size && lhs.batch_dirs_count == rhs.batch_block_size &&
           lhs.temporally_timeout == rhs.temporally_timeout;
}

bool operator==(const global_announce_config_t &lhs, const global_announce_config_t &rhs) noexcept {
    return lhs.enabled == rhs.enabled && lhs.announce_url == rhs.announce_url && lhs.device_id == rhs.device_id &&
           lhs.cert_file == rhs.cert_file && lhs.key_file == rhs.key_file && lhs.rx_buff_size == rhs.rx_buff_size &&
           lhs.timeout == rhs.timeout && lhs.reannounce_after == rhs.reannounce_after;
}

bool operator==(const local_announce_config_t &lhs, const local_announce_config_t &rhs) noexcept {
    return lhs.enabled == rhs.enabled && lhs.port == rhs.port && lhs.frequency == rhs.frequency;
}

bool operator==(const log_config_t &lhs, const log_config_t &rhs) noexcept {
    return lhs.name == rhs.name && lhs.level == rhs.level && lhs.sinks == rhs.sinks;
}

bool operator==(const main_t &lhs, const main_t &rhs) noexcept {
    return lhs.local_announce_config == rhs.local_announce_config && lhs.upnp_config == rhs.upnp_config &&
           lhs.global_announce_config == rhs.global_announce_config && lhs.bep_config == rhs.bep_config &&
           lhs.tui_config == rhs.tui_config && lhs.timeout == rhs.timeout && lhs.device_name == rhs.device_name &&
           lhs.config_path == rhs.config_path && lhs.log_configs == rhs.log_configs &&
           lhs.hasher_threads == rhs.hasher_threads;
}

bool operator==(const tui_config_t &lhs, const tui_config_t &rhs) noexcept {
    return lhs.refresh_interval == rhs.refresh_interval && lhs.key_quit == rhs.key_quit &&
           lhs.key_more_logs == rhs.key_more_logs && lhs.key_less_logs == rhs.key_less_logs &&
           lhs.key_help == rhs.key_help && lhs.key_config == rhs.key_config;
}

bool operator==(const upnp_config_t &lhs, const upnp_config_t &rhs) noexcept {
    return lhs.discovery_attempts == rhs.discovery_attempts && lhs.max_wait == rhs.max_wait &&
           lhs.timeout == rhs.timeout && lhs.external_port == rhs.external_port && lhs.rx_buff_size == rhs.rx_buff_size;
}

using device_name_t = outcome::result<std::string>;

static std::string expand_home(const std::string &path, const char *home) {
    if (home && path.size() && path[0] == '~') {
        std::string new_path(home);
        new_path += path.c_str() + 1;
        return new_path;
    }
    return path;
}

static device_name_t get_device_name() noexcept {
    sys::error_code ec;
    auto device_name = boost::asio::ip::host_name(ec);
    if (ec) {
        return ec;
    }
    return device_name;
}

config_result_t get_config(std::istream &config, const boost::filesystem::path &config_path) {
    main_t cfg;
    cfg.config_path = config_path;

    auto home = std::getenv("HOME");
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

        auto url = t["announce_url"].value<std::string>();
        if (!url) {
            return "global_discovery/announce_url is incorrect or missing";
        }
        auto announce_url = utils::parse(url.value().c_str());
        if (!announce_url) {
            return "global_discovery/announce_url is not url";
        }
        c.announce_url = announce_url.value();

        auto device_id = t["device_id"].value<std::string>();
        if (!device_id) {
            return "global_discovery/device_id is incorrect or missing";
        }
        c.device_id = device_id.value();

        auto cert_file = t["cert_file"].value<std::string>();
        if (!cert_file) {
            return "global_discovery/cert_file is incorrect or missing";
        }
        c.cert_file = expand_home(cert_file.value(), home);

        auto key_file = t["key_file"].value<std::string>();
        if (!key_file) {
            return "global_discovery/key_file is incorrect or missing";
        }
        c.key_file = expand_home(key_file.value(), home);

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
        auto max_wait = t["max_wait"].value<std::uint32_t>();
        if (!max_wait) {
            return "global_discovery/max_wait is incorrect or missing";
        }
        c.max_wait = max_wait.value();

        auto discovery_attempts = t["discovery_attempts"].value<std::uint32_t>();
        if (!discovery_attempts) {
            return "global_discovery/discovery_attempts is incorrect or missing";
        }
        c.discovery_attempts = discovery_attempts.value();

        auto timeout = t["timeout"].value<std::uint32_t>();
        if (!timeout) {
            return "upnp/timeout is incorrect or missing";
        }
        c.timeout = timeout.value();

        auto external_port = t["external_port"].value<std::uint32_t>();
        if (!external_port) {
            return "upnp/external_port is incorrect or missing";
        }
        c.external_port = external_port.value();

        auto rx_buff_size = t["rx_buff_size"].value<std::uint32_t>();
        if (!rx_buff_size) {
            return "upng/rx_buff_size is incorrect or missing";
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
    }

    // fs
    {
        auto t = root_tbl["fs"];
        auto &c = cfg.fs_config;

        auto batch_block_size = t["batch_block_size"].value<std::uint32_t>();
        if (!batch_block_size) {
            return "fs/batch_block_size is incorrect or missing";
        }
        c.batch_block_size = batch_block_size.value();

        auto batch_dirs_count = t["batch_dirs_count"].value<std::uint32_t>();
        if (!batch_dirs_count) {
            return "fs/batch_dirs_count is incorrect or missing";
        }
        c.batch_dirs_count = batch_dirs_count.value();

        auto temporally_timeout = t["temporally_timeout"].value<std::uint32_t>();
        if (!temporally_timeout) {
            return "fs/temporally_timeout is incorrect or missing";
        }
        c.temporally_timeout = temporally_timeout.value();
    }

    // tui
    {
        auto t = root_tbl["tui"];
        auto &c = cfg.tui_config;
        auto refresh_interval = t["refresh_interval"].value<std::uint32_t>();
        if (!refresh_interval) {
            return "tui/refresh_interval is incorrect or missing";
        }
        c.refresh_interval = refresh_interval.value();

        auto key_quit = t["key_quit"].value<std::string>();
        if (!key_quit || key_quit.value().empty()) {
            return "tui/key_quit is incorrect or missing";
        }
        c.key_quit = key_quit.value()[0];

        auto key_more_logs = t["key_more_logs"].value<std::string>();
        if (!key_more_logs || key_more_logs.value().empty()) {
            return "tui/key_more_logs is incorrect or missing";
        }
        c.key_more_logs = key_more_logs.value()[0];

        auto key_less_logs = t["key_less_logs"].value<std::string>();
        if (!key_less_logs || key_less_logs.value().empty()) {
            return "tui/key_less_logs is incorrect or missing";
        }
        c.key_less_logs = key_less_logs.value()[0];

        auto key_config = t["key_config"].value<std::string>();
        if (!key_config || key_config.value().empty()) {
            return "tui/key_config is incorrect or missing";
        }
        c.key_config = key_config.value()[0];

        auto key_help = t["key_help"].value<std::string>();
        if (!key_help || key_help.value().empty()) {
            return "tui/key_help is incorrect or missing";
        }
        c.key_help = key_help.value()[0];
    }

    return std::move(cfg);
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
                                 {"announce_url", cfg.global_announce_config.announce_url.full},
                                 {"device_id", cfg.global_announce_config.device_id},
                                 {"cert_file", cfg.global_announce_config.cert_file},
                                 {"key_file", cfg.global_announce_config.key_file},
                                 {"rx_buff_size", cfg.global_announce_config.rx_buff_size},
                                 {"timeout", cfg.global_announce_config.timeout},
                             }}},
        {"upnp", toml::table{{
                     {"discovery_attempts", cfg.upnp_config.discovery_attempts},
                     {"max_wait", cfg.upnp_config.max_wait},
                     {"timeout", cfg.upnp_config.timeout},
                     {"external_port", cfg.upnp_config.external_port},
                     {"rx_buff_size", cfg.upnp_config.rx_buff_size},
                 }}},
        {"bep", toml::table{{
                    {"rx_buff_size", cfg.bep_config.rx_buff_size},
                    {"connect_timeout", cfg.bep_config.connect_timeout},
                    {"request_timeout", cfg.bep_config.request_timeout},
                    {"tx_timeout", cfg.bep_config.tx_timeout},
                    {"rx_timeout", cfg.bep_config.rx_timeout},
                }}},
        {"dialer", toml::table{{
                       {"enabled", cfg.dialer_config.enabled},
                       {"redial_timeout", cfg.dialer_config.redial_timeout},
                   }}},
        {"fs", toml::table{{
                   {"batch_block_size", cfg.fs_config.batch_block_size},
                   {"batch_dirs_count", cfg.fs_config.batch_dirs_count},
                   {"temporally_timeout", cfg.fs_config.temporally_timeout},
               }}},
        {"tui", toml::table{{
                    {"refresh_interval", cfg.tui_config.refresh_interval},
                    {"key_quit", std::string_view(&cfg.tui_config.key_quit, 1)},
                    {"key_more_logs", std::string_view(&cfg.tui_config.key_more_logs, 1)},
                    {"key_less_logs", std::string_view(&cfg.tui_config.key_less_logs, 1)},
                    {"key_config", std::string_view(&cfg.tui_config.key_config, 1)},
                    {"key_help", std::string_view(&cfg.tui_config.key_help, 1)},
                }}},
    }};
    // clang-format on
    out << tbl;
    return outcome::success();
}

outcome::result<main_t> generate_config(const boost::filesystem::path &config_path) {
    auto dir = config_path.parent_path();
    sys::error_code ec;
    bool exists = bfs::exists(dir, ec);
    if (!exists) {
        spdlog::info("creating directory {}", dir.c_str());
        bfs::create_directories(dir, ec);
        if (ec) {
            spdlog::error("cannot create dirs: {}", ec);
            return ec;
        }
    }
    std::string cert_file = home_path + "/cert.pem";
    std::string key_file = home_path + "/key.pem";
    auto home = std::getenv("HOME");
    auto config_dir = bfs::path(home).append(".config").append("syncthing");
    bool is_home = dir == bfs::path(config_dir);
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
    cfg.default_location = bfs::path(home);
    cfg.timeout = 5000;
    cfg.device_name = device;
    cfg.hasher_threads = 3;
    cfg.log_configs = {
        log_config_t {
            "default", spdlog::level::level_enum::info, {"stdout"}
        }
    };
    cfg.local_announce_config = local_announce_config_t {
        true,
        21027,
        30
    };
    cfg.global_announce_config = global_announce_config_t{
        true,
        utils::parse("https://discovery.syncthing.net/").value(),
        "LYXKCHX-VI3NYZR-ALCJBHF-WMZYSPK-QG6QJA3-MPFYMSO-U56GTUK-NA2MIAW",
        cert_file,
        key_file,
        32 * 1024,
        3000,
        10 * 60,
    };
    cfg.upnp_config = upnp_config_t {
        2,          /* discovery_attempts */
        1,          /* max_wait */
        10,         /* timeout */
        22001,      /* external port */
        64 * 1024,  /* rx_buff */
    };
    cfg.bep_config = bep_config_t {
        16 * 1024 * 1024,   /* rx_buff */
        5000,               /* connect_timeout */
        60000,              /* request_timeout */
        90000,              /* tx_timeout */
        300000,             /* rx_timeout */
    };
    cfg.tui_config = tui_config_t {
        100,   /* refresh_interval */
        'q',   /* key_quit */
        '+',   /* key_more_logs */
        '-',   /* key_less_logs */
        '?',   /* key_help */
        'c'    /* key_config */
    };
    cfg.dialer_config = dialer_config_t {
        true,       /* enabled */
        5 * 60000   /* redial timeout */
    };
    cfg.fs_config = fs_config_t {
        16777216,   /* 16MB, batch_block_size */
        10,         /* batch_dirs_count */
        86400000    /* temporally_timeout, 24h default */ 
    };
    return cfg;
}


}
