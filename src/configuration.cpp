#include "configuration.h"
#include <boost/program_options.hpp>
#include <boost/tokenizer.hpp>

namespace po = boost::program_options;

namespace syncspirit::config {

spdlog::level::level_enum get_log_level(const std::string &log_level) {
    using namespace spdlog::level;
    level_enum value = info;
    if (log_level == "trace")
        value = trace;
    if (log_level == "debug")
        value = debug;
    if (log_level == "info")
        value = info;
    if (log_level == "warn")
        value = warn;
    if (log_level == "error")
        value = err;
    if (log_level == "crit")
        value = critical;
    if (log_level == "off")
        value = off;
    return value;
}

static void fill_logging_config(po::variables_map &vm, configuration_t &cfg) {
    // sinks
    boost::optional<console_sink_t> console_sink;
    if (vm.count("sink_console.level")) {
        auto level = get_log_level(vm["sink_console.level"].as<std::string>());
        console_sink = console_sink_t{level};
    }

    boost::optional<file_sink_t> file_sink;
    if (vm.count("sink_file.file")) {
        auto file = vm["sink_file.file"].as<std::string>();
        auto level = get_log_level(vm["sink_file.level"].as<std::string>());
        file_sink = file_sink_t{level, file};
    }

    if (vm.count("logging.sinks")) {
        using tokenizer = boost::tokenizer<boost::char_separator<char>>;
        auto sinks = vm["logging.sinks"].as<std::string>();
        boost::char_separator<char> separator(",");
        tokenizer tokens(sinks, separator);
        for (auto &sink_name : tokens) {
            if (sink_name == "console" && console_sink) {
                cfg.logging_config.sinks.push_back(*console_sink);
            }
            if (sink_name == "file" && file_sink) {
                cfg.logging_config.sinks.push_back(*file_sink);
            }
        }
    }
}

boost::optional<configuration_t> get_config(std::ifstream &config) {
    using result_t = boost::optional<configuration_t>;
    result_t result{};
    configuration_t cfg;

    // clang-format off
    po::options_description descr("Allowed options");
    descr.add_options()
            ("sink_console.level", po::value<std::string>()->default_value("info"),"log level (default: info)")
            ("sink_file.level", po::value<std::string>()->default_value("info"), "log level (default: info)")
            ("sink_file.file", po::value<std::string>(), "log file")
            ("logging.sinks", po::value<std::string>()->default_value("console"), "comma-separated list of sinks, mandatory")
            ("local_announce.enabled", po::value<bool>()->default_value(true), "enable LAN-announcements")
            ("local_announce.port", po::value<std::uint16_t>()->default_value(21027), "LAN-announcement port")
            ("global_discovery.server", po::value<std::string>()->default_value("https://discovery.syncthing.net/v2/?noannounce&id=LYXKCHX-VI3NYZR-ALCJBHF-WMZYSPK-QG6QJA3-MPFYMSO-U56GTUK-NA2MIAW"), "Global announce server")
            ("global_discovery.cert_file", po::value<std::string>()->default_value("~/.config/syncthing/cert.pem"), "certificate file path")
            ("global_discovery.key_file", po::value<std::string>()->default_value("~/.config/syncthing/key.pem"), "key file path")
            ("global_discovery.timeout", po::value<std::uint32_t>()->default_value(1), "discovery timeout (seconds)")
            ("upnp.timeout", po::value<std::uint32_t>()->default_value(5), "max wait discovery timeout")
            ;
    // clang-format on

    po::variables_map vm;
    po::store(po::parse_config_file(config, descr, false), vm);
    po::notify(vm);

    fill_logging_config(vm, cfg);

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

    cfg.upnp_config.timeout = vm["upnp.timeout"].as<std::uint32_t>();

    // checks
    if (cfg.logging_config.sinks.empty()) {
        std::cout << descr << "\n";
        return result;
    }

    // all OK
    result = std::move(cfg);

    return result;
}

} // namespace syncspirit::config
