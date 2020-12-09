#include "configuration.h"
#include <boost/asio/ip/host_name.hpp>
#include <boost/tokenizer.hpp>
#include <boost/regex.hpp>
#include <boost/algorithm/string.hpp>
#include <spdlog/spdlog.h>
#include <cstdlib>

#define TOML_EXCEPTIONS 0
#include <toml++/toml.h>

namespace fs = boost::filesystem;

static const std::string home_path = "~/.config/syncspirit";

namespace syncspirit::config {

static std::string expand_home(const std::string &path, const char *home) {
    if (home && path.size() && path[0] == '~') {
        std::string new_path(home);
        new_path += path.c_str() + 1;
        return new_path;
    }
    return path;
}

using device_name_t = outcome::result<std::string>;

static device_name_t get_device_name() noexcept {
    boost::system::error_code ec;
    auto device_name = boost::asio::ip::host_name(ec);
    if (ec) {
        return ec;
    }
    return device_name;
}

config_result_t get_config(std::istream &config) {
    configuration_t cfg;

    auto home = std::getenv("HOME");
    auto r = toml::parse(config);
    if (!r) {
        return std::string(r.error().description());
    }

    auto &root_tbl = r.table();
    // global
    do {
        auto t = root_tbl["global"];
        auto timeout = t["timeout"].value<std::uint32_t>();
        if (!timeout) {
            return "global/timeout is incorrect or missing";
        }
        cfg.timeout = timeout.value();

        auto device_name = t["device_name"].value<std::string>();
        if (!device_name) {
            auto option = get_device_name();
            if (!option)
                return option.error().message();
            device_name = option.value();
        }
        cfg.device_name = device_name.value();
    } while (0);

    // local_discovery
    do {
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
    } while (0);

    // global_discovery
    do {
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
    } while (0);

    // upnp
    do {
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

    } while (0);

    // bep
    do {
        auto t = root_tbl["bep"];
        auto &c = cfg.bep_config;
        auto rx_buff_size = t["rx_buff_size"].value<std::uint32_t>();
        if (!rx_buff_size) {
            return "bep/rx_buff_size is incorrect or missing";
        }
        c.rx_buff_size = rx_buff_size.value();
    } while (0);
    return std::move(cfg);
}

outcome::result<void> serialize(const configuration_t cfg, std::ostream &out) noexcept {
    // clang-format off
    auto tbl = toml::table{{
        {"global", toml::table{{
            {"timeout",  cfg.timeout},
            {"device_name", cfg.device_name}
        }}},
        {"local_discovery", toml::table{{
            {"enabled",  cfg.local_announce_config.enabled},
            {"port",  cfg.local_announce_config.port},
            {"frequency",  cfg.local_announce_config.frequency},
        }}},
        {"global_discovery", toml::table{{
            {"enabled",  cfg.global_announce_config.enabled},
            {"announce_url", cfg.global_announce_config.announce_url.full},
            {"device_id", cfg.global_announce_config.device_id},
            {"cert_file", cfg.global_announce_config.cert_file},
            {"key_file", cfg.global_announce_config.key_file},
            {"rx_buff_size", cfg.global_announce_config.rx_buff_size},
            {"timeout",  cfg.global_announce_config.timeout},
        }}},
        {"upnp", toml::table{{
            {"discovery_attempts", cfg.upnp_config.discovery_attempts},
            {"max_wait", cfg.upnp_config.max_wait},
            {"timeout",  cfg.upnp_config.timeout},
            {"external_port",  cfg.upnp_config.external_port},
            {"rx_buff_size", cfg.upnp_config.rx_buff_size},
        }}},
        {"bep", toml::table{{
            {"rx_buff_size", cfg.bep_config.rx_buff_size},
        }}},
    }};
    // clang-format on
    out << tbl;
    return outcome::success();
}

configuration_t generate_config(const boost::filesystem::path &config_path) {
    auto dir = config_path.parent_path();
    if (!fs::exists(dir)) {
        spdlog::info("creating directory {}", dir.c_str());
        fs::create_directories(dir);
    }

    std::string cert_file = home_path + "/cert.pem";
    std::string key_file = home_path + "/key.pem";
    auto home = std::getenv("HOME");
    auto home_dir = fs::path(home).append(".config").append("syncthing");
    bool is_home = dir == fs::path(home_dir);
    if (!is_home) {
        using boost::algorithm::replace_all_copy;
        cert_file = replace_all_copy(cert_file, home_path, dir.string());
        key_file = replace_all_copy(key_file, home_path, dir.string());
    }

    auto device_name = get_device_name();
    auto device = std::string(device_name ? device_name.value() : "localhost");

    // clang-format off
    configuration_t cfg;
    cfg.timeout = 5000;
    cfg.device_name = device;
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
    };
    return cfg;
}

} // namespace syncspirit::config
