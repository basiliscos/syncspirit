#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>

#include <google/protobuf/stubs/common.h>
#include <lz4.h>
#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>
#include <rotor/asio.hpp>
#include <rotor/thread.hpp>
#include <spdlog/spdlog.h>

#include "constants.h"
#include "config/utils.h"
#include "utils/location.h"
#include "net/net_supervisor.h"
#include "console/sink.h"
#include "console/tui_actor.h"
#include "console/utils.h"
#include "fs/fs_supervisor.h"

namespace bfs = boost::filesystem;
namespace po = boost::program_options;
namespace pt = boost::posix_time;
namespace ra = rotor::asio;
namespace rth = rotor::thread;
namespace asio = boost::asio;

using namespace syncspirit;

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

int main(int argc, char **argv) {
    GOOGLE_PROTOBUF_VERIFY_VERSION;

    console::term_prepare();
    try {
        // clang-format off
        /* parse command-line & config options */
        po::options_description cmdline_descr("Allowed options");
        cmdline_descr.add_options()
                ("help", "show this help message")
                ("log_level", po::value<std::string>()->default_value("info"), "initial log level")
                ("config_dir", po::value<std::string>(), "configuration directory path");
        // clang-format on

        po::variables_map vm;
        po::store(po::parse_command_line(argc, argv, cmdline_descr), vm);
        po::notify(vm);

        bool show_help = vm.count("help");
        if (show_help) {
            std::cout << cmdline_descr << "\n";
            return 1;
        }

        auto log_level_str = vm["log_level"].as<std::string>();
        auto log_level = get_log_level(log_level_str);
        std::mutex std_out_mutex;
        std::string prompt = "> ";
        auto console_sink =
            std::make_shared<console::sink_t>(stdout, spdlog::color_mode::automatic, std_out_mutex, prompt);
        spdlog::set_default_logger(std::make_shared<spdlog::logger>("", console_sink));
        spdlog::set_level(log_level);

        bfs::path config_file_path;
        if (vm.count("config_dir")) {
            auto path = vm["config_dir"].as<std::string>();
            config_file_path = bfs::path{path.c_str()};
        } else {
            auto config_default = utils::get_default_config_dir();
            if (config_default) {
                config_file_path = config_default.value();
            } else {
                spdlog::error("cannot determine default config dir: {}", config_default.error().message());
                return 1;
            }
        }

        config_file_path.append("syncspirit.toml");
        bool populate = !bfs::exists(config_file_path);
        if (populate) {
            spdlog::info("Config {} seems does not exit, creating default one...", config_file_path.c_str());
            auto cfg = config::generate_config(config_file_path);
            std::fstream f_cfg(config_file_path.string(), f_cfg.binary | f_cfg.trunc | f_cfg.in | f_cfg.out);
            auto r = config::serialize(cfg, f_cfg);
            if (!r) {
                spdlog::error("cannot save default config at :: {}", r.error().message());
                return -1;
            }
        }
        auto config_file_path_c = config_file_path.c_str();
        std::ifstream config_file(config_file_path_c);
        if (!config_file) {
            spdlog::error("Cannot open config file {}", config_file_path_c);
            return 1;
        }

        config::config_result_t cfg_option = config::get_config(config_file, config_file_path.parent_path());
        if (!cfg_option) {
            spdlog::error("Config file {} is incorrect :: {}", config_file_path_c, cfg_option.error());
            return 1;
        }
        auto &cfg = cfg_option.value();
        spdlog::trace("configuration seems OK");

        if (populate) {
            spdlog::info("Generating cryptographical keys...");
            auto pair = utils::generate_pair(constants::issuer_name);
            if (!pair) {
                spdlog::error("cannot generate cryptographical keys :: {}", pair.error().message());
                return -1;
            }
            auto &keys = pair.value();
            auto &cert_path = cfg.global_announce_config.cert_file;
            auto &key_path = cfg.global_announce_config.key_file;
            auto save_result = keys.save(cert_path.c_str(), key_path.c_str());
            if (!save_result) {
                spdlog::error("cannot store cryptographical keys :: {}", save_result.error().message());
                return -1;
            }
        }

        spdlog::info("starting {} {}, libraries: protobuf v{}, lz4: v{}", constants::client_name,
                     constants::client_version, google::protobuf::internal::VersionString(GOOGLE_PROTOBUF_VERSION),
                     LZ4_versionString());

        /* pre-init actors */
        asio::io_context io_context;
        ra::system_context_ptr_t sys_context{new ra::system_context_asio_t{io_context}};
        auto stand = std::make_shared<asio::io_context::strand>(io_context);
        auto timeout = pt::milliseconds{cfg.timeout};
        auto sup_net = sys_context->create_supervisor<net::net_supervisor_t>()
                           .app_config(cfg)
                           .strand(stand)
                           .timeout(timeout)
                           .create_registry()
                           .guard_context(true)
                           .finish();
        sup_net->start();

        rth::system_context_thread_t fs_context;
        auto fs_sup = fs_context.create_supervisor<syncspirit::fs::fs_supervisor_t>()
                          .timeout(timeout)
                          .registry_address(sup_net->get_registry_address())
                          .finish();

        /* launch actors */
        auto net_thread = std::thread([&]() {
            io_context.run();
            console::shutdown_flag = true;
            spdlog::trace("net thread has been terminated");
        });

        auto fs_thread = std::thread([&]() {
            fs_context.run();
            console::shutdown_flag = true;
            spdlog::trace("fs thread has been terminated");
        });

        asio::io_context console_context;
        ra::system_context_asio_t con_context{console_context};
        auto con_stand = std::make_shared<asio::io_context::strand>(console_context);
        auto sup_con = con_context.create_supervisor<ra::supervisor_asio_t>()
                           .strand(con_stand)
                           .timeout(timeout)
                           .registry_address(sup_net->get_registry_address())
                           .guard_context(true)
                           .finish();
        sup_con->start();
        sup_con->create_actor<console::tui_actor_t>()
            .mutex(&std_out_mutex)
            .prompt(&prompt)
            .tui_config(cfg.tui_config)
            .timeout(timeout)
            .finish();
        console_context.run();

        spdlog::trace("waiting fs thread termination");
        fs_thread.join();

        spdlog::trace("waiting net thread termination");
        net_thread.join();

        spdlog::trace("everything has been terminated");
    } catch (...) {
        spdlog::critical("unknown exception");
        // spdlog::critical("Starting failure : {}", ex.what());
        return 1;
    }

    google::protobuf::ShutdownProtobufLibrary();
    /* exit */
    spdlog::info("normal exit");
    spdlog::drop_all();
    return 0;
}
