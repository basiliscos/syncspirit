#include "configuration.h"
#include <boost/program_options.hpp>
#include <boost/tokenizer.hpp>

namespace po = boost::program_options;

namespace syncspirit::config {

boost::optional<configuration_t> get_config(std::ifstream &config) {
    using result_t = boost::optional<configuration_t>;
    result_t result{};
    configuration_t cfg;

    // clang-format off
    po::options_description descr("Allowed options");
    descr.add_options()
            ("global.timeout", po::value<std::uint32_t>()->default_value(200), "root timeout in milliseconds (default: 200)")
            ("local_announce.enabled", po::value<bool>()->default_value(true), "enable LAN-announcements")
            ("local_announce.port", po::value<std::uint16_t>()->default_value(21027), "LAN-announcement port")
            ("global_discovery.server", po::value<std::string>()->default_value("https://discovery.syncthing.net/v2/?noannounce&id=LYXKCHX-VI3NYZR-ALCJBHF-WMZYSPK-QG6QJA3-MPFYMSO-U56GTUK-NA2MIAW"), "Global announce server")
            ("global_discovery.cert_file", po::value<std::string>()->default_value("~/.config/syncthing/cert.pem"), "certificate file path")
            ("global_discovery.key_file", po::value<std::string>()->default_value("~/.config/syncthing/key.pem"), "key file path")
            ("global_discovery.timeout", po::value<std::uint32_t>()->default_value(1), "discovery timeout (seconds)")
            ("upnp.rx_buff_size", po::value<std::uint32_t>()->default_value(16384), "receive bufffer size in bytes (default: 16384)")
            ("upnp.max_wait", po::value<std::uint32_t>()->default_value(5), "max wait discovery timeout")
            ("upnp.timeout", po::value<std::uint32_t>()->default_value(5), "total upnp timeout")
            ("upnp.external_port", po::value<std::uint16_t>()->default_value(21028), "external port to accept connections (default: 21028)")
            ;
    // clang-format on

    po::variables_map vm;
    po::store(po::parse_config_file(config, descr, false), vm);
    po::notify(vm);

    cfg.timeout = vm["global.timeout"].as<std::uint32_t>();
    cfg.local_announce_config.enabled = vm["local_announce.enabled"].as<bool>();
    cfg.local_announce_config.port = vm["local_announce.port"].as<std::uint16_t>();

    auto server_url_raw = vm["global_discovery.server"].as<std::string>();
    auto server_url = utils::parse(server_url_raw.c_str());
    if (!server_url) {
        std::cout << "wrong server_url: " << server_url_raw << "\n";
        std::cout << descr << "\n";
        return result;
    }
    cfg.global_announce_config.server_url = *server_url;
    cfg.global_announce_config.cert_file = vm["global_discovery.cert_file"].as<std::string>();
    cfg.global_announce_config.key_file = vm["global_discovery.key_file"].as<std::string>();
    cfg.global_announce_config.timeout = vm["global_discovery.timeout"].as<std::uint32_t>();

    cfg.upnp_config.max_wait = vm["upnp.max_wait"].as<std::uint32_t>();
    cfg.upnp_config.timeout = vm["upnp.timeout"].as<std::uint32_t>();
    cfg.upnp_config.external_port = vm["upnp.external_port"].as<std::uint16_t>();


    // all OK
    result = std::move(cfg);

    return result;
}

} // namespace syncspirit::config
