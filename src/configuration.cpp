#include "configuration.h"
#include <boost/asio/ip/host_name.hpp>
#include <boost/program_options.hpp>
#include <boost/tokenizer.hpp>
#include <boost/regex.hpp>
#include <boost/algorithm/string.hpp>
#include <spdlog/spdlog.h>
#include <cstdlib>
#include <sstream>
#include <fstream>

namespace po = boost::program_options;
namespace fs = boost::filesystem;

namespace syncspirit::config {

static std::string expand_home(const std::string &path, const char *home) {
    if (home && path.size() && path[0] == '~') {
        std::string new_path(home);
        new_path += path.c_str() + 1;
        return new_path;
    }
    return path;
}

static po::options_description get_default_options() noexcept {
    // clang-format off
    po::options_description descr("Allowed options");
    descr.add_options()
            ("global.timeout", po::value<std::uint32_t>()->default_value(5000), "root timeout in milliseconds (default: 200)")
            ("global.device_name", po::value<std::string>()->default_value("%localhost%"), "human-readeable this device identity")
            ("local_announce.enabled", po::value<bool>()->default_value(false), "enable LAN-announcements")
            ("local_announce.port", po::value<std::uint16_t>()->default_value(21027), "LAN-announcement port")
            ("global_discovery.announce_url", po::value<std::string>()->default_value("https://discovery.syncthing.net/v2"), "Global announce server")
            ("global_discovery.device_id", po::value<std::string>()->default_value("LYXKCHX-VI3NYZR-ALCJBHF-WMZYSPK-QG6QJA3-MPFYMSO-U56GTUK-NA2MIAW"), "discovery server certificate device id")
            ("global_discovery.cert_file", po::value<std::string>()->default_value("~/.config/syncspirit/cert.pem"), "certificate file path")
            ("global_discovery.key_file", po::value<std::string>()->default_value("~/.config/syncspirit/key.pem"), "key file path")
            ("global_discovery.rx_buff_size", po::value<std::uint32_t>()->default_value(16 * 1024 * 1024), "rx buff size in bytes (default: 16 Mb)")
            ("global_discovery.timeout", po::value<std::uint32_t>()->default_value(3000), "timeout in milliseconds (default: 3000)")
            ("global_discovery.reannounce_after", po::value<std::uint32_t>()->default_value(600000), "timeout after reannounce in milliseconds (default: 600000)")
            ("upnp.rx_buff_size", po::value<std::uint32_t>()->default_value(16384), "receive buffer size in bytes (default: 16384)")
            ("upnp.max_wait", po::value<std::uint32_t>()->default_value(1), "max wait discovery timeout (default: 1 second)")
            ("upnp.timeout", po::value<std::uint32_t>()->default_value(10), "total upnp timeout (default: 10 seconds)")
            ("upnp.external_port", po::value<std::uint16_t>()->default_value(22001), "external port to accept connections (default: 22001)")
            ("bep.rx_buff_size", po::value<std::uint64_t>()->default_value(16 * 1024 * 1024), "receive buffer size in bytes (default: 16 Mb)")
            ;
    // clang-format on
    return descr;
}

static std::string get_device_name() noexcept {
    boost::system::error_code ec;
    auto device_name = boost::asio::ip::host_name(ec);
    if (ec) {
        spdlog::warn("hostname cannot be determined: {}", ec.message());
        device_name = "localhost";
    }
    return device_name;
}

config_option_t get_config(std::ifstream &config) {
    config_option_t result{};
    configuration_t cfg;

    auto home = std::getenv("HOME");
    auto descr = get_default_options();

    po::variables_map vm;
    po::store(po::parse_config_file(config, descr, false), vm);
    po::notify(vm);

    auto device_name = vm["global.device_name"].as<std::string>();
    if (device_name.empty()) {
        device_name = get_device_name();
    }
    cfg.timeout = vm["global.timeout"].as<std::uint32_t>();
    cfg.device_name = device_name;
    cfg.local_announce_config.enabled = vm["local_announce.enabled"].as<bool>();
    cfg.local_announce_config.port = vm["local_announce.port"].as<std::uint16_t>();

    auto announce_url_raw = vm["global_discovery.announce_url"].as<std::string>();
    auto announce_url = utils::parse(announce_url_raw.c_str());
    if (!announce_url) {
        std::cout << "wrong announce_url: " << announce_url_raw << "\n";
        std::cout << descr << "\n";
        return result;
    }
    cfg.global_announce_config.announce_url = *announce_url;
    cfg.global_announce_config.device_id = vm["global_discovery.device_id"].as<std::string>();
    cfg.global_announce_config.cert_file = expand_home(vm["global_discovery.cert_file"].as<std::string>(), home);
    cfg.global_announce_config.key_file = expand_home(vm["global_discovery.key_file"].as<std::string>(), home);
    cfg.global_announce_config.rx_buff_size = vm["global_discovery.rx_buff_size"].as<std::uint32_t>();
    cfg.global_announce_config.timeout = vm["global_discovery.timeout"].as<std::uint32_t>();
    cfg.global_announce_config.reannounce_after = vm["global_discovery.reannounce_after"].as<std::uint32_t>();

    cfg.upnp_config.max_wait = vm["upnp.max_wait"].as<std::uint32_t>();
    cfg.upnp_config.timeout = vm["upnp.timeout"].as<std::uint32_t>();
    cfg.upnp_config.external_port = vm["upnp.external_port"].as<std::uint16_t>();

    cfg.bep_config.rx_buff_size = vm["bep.rx_buff_size"].as<std::uint64_t>();

    // all OK
    result = std::move(cfg);

    return result;
}

void populate_config(const fs::path &config_path) {
    auto dir = config_path.parent_path();
    if (!fs::exists(dir)) {
        spdlog::info("creating directory {}", dir.c_str());
        fs::create_directories(dir);
    }
    auto descr = get_default_options();

    // used to get the defaults
    std::stringstream empty_in;
    po::variables_map vm;
    po::store(po::parse_config_file(empty_in, descr, false), vm);

    std::stringstream out_str;
    std::ostream &out = out_str;

    std::string last_section;

    for (auto &opt : descr.options()) {
        std::string value_str;
        auto &value_container = vm[opt->long_name()];
        auto &value = value_container.value();
        if (value.type() == typeid(std::string)) {
            value_str = value_container.as<std::string>();
        } else if (value.type() == typeid(bool)) {
            value_str = value_container.as<bool>() ? "true" : "false";
        } else if (value.type() == typeid(std::uint16_t)) {
            value_str = std::to_string(value_container.as<std::uint16_t>());
        } else if (value.type() == typeid(std::uint32_t)) {
            value_str = std::to_string(value_container.as<std::uint32_t>());
        } else if (value.type() == typeid(std::uint64_t)) {
            value_str = std::to_string(value_container.as<std::uint64_t>());
        }
        auto long_name = opt->long_name();
        auto dot = long_name.find(".");
        assert(dot > 0 && dot != std::string::npos);

        std::string section(long_name, 0, dot);
        std::string name(long_name, dot + 1);
        if (section != last_section) {
            out << "\n[" << section << "]\n";
            last_section = std::move(section);
        }

        out << "# " << opt->description() << "\n";
        out << name << " = " << value_str << "\n\n";
    }

    // special value handling
    auto result = boost::algorithm::replace_all_copy(out_str.str(), "%localhost%", get_device_name());

    auto home = std::getenv("HOME");
    auto home_dir = fs::path(home).append(".config").append("syncthing");
    bool is_home = dir == fs::path(home_dir);
    if (!is_home) {
        result = boost::algorithm::replace_all_copy(result, "~/.config/syncspirit", dir.string());
    }

    std::fstream f_out(config_path.string(), f_out.binary | f_out.trunc | f_out.in | f_out.out);
    f_out << result;
}

} // namespace syncspirit::config
