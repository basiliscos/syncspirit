#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>

#include <google/protobuf/stubs/common.h>
#include <lz4.h>
#include <openssl/crypto.h>
#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>
#include <rotor/asio.hpp>
#include <rotor/thread.hpp>
#include <spdlog/spdlog.h>

#include "constants.h"
#include "config/utils.h"
#include "utils/location.h"
#include "utils/log.h"
#include "net/net_supervisor.h"
#include "console/tui_actor.h"
#include "console/utils.h"
#include "fs/fs_supervisor.h"

#if defined(__linux__)
#include <pthread.h>
#endif

namespace bfs = boost::filesystem;
namespace po = boost::program_options;
namespace pt = boost::posix_time;
namespace ra = rotor::asio;
namespace rth = rotor::thread;
namespace asio = boost::asio;

using namespace syncspirit;

int main(int argc, char **argv) {
    GOOGLE_PROTOBUF_VERIFY_VERSION;
    std::string prompt = "> ";

    try {
#if defined(__linux__)
        pthread_setname_np(pthread_self(), "synspirit/main");
#endif
        // clang-format off
        /* parse command-line & config options */
        po::options_description cmdline_descr("Allowed options");
        cmdline_descr.add_options()
                ("help", "show this help message")
                ("log_level", po::value<std::string>()->default_value("info"), "initial log level")
                ("config_dir", po::value<std::string>(), "configuration directory path")
                ("interactive", po::value<bool>()->default_value(true), "allow interactions with user via stdin");
        // clang-format on

        po::variables_map vm;
        po::store(po::parse_command_line(argc, argv, cmdline_descr), vm);
        po::notify(vm);

        bool show_help = vm.count("help");
        if (show_help) {
            std::cout << cmdline_descr << "\n";
            return 1;
        }

        bool interactive = vm["interactive"].as<bool>();
        if (interactive) {
            console::term_prepare();
        }

        std::mutex std_out_mutex;
        utils::set_default(vm["log_level"].as<std::string>(), prompt, std_out_mutex, interactive);

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
            auto cfg_opt = config::generate_config(config_file_path);
            if (!cfg_opt) {
                spdlog::error("cannot generate default config: {}", cfg_opt.error().message());
                return -1;
            }
            auto &cfg = cfg_opt.value();
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

        bool overwrite_default = vm.count("log_level");
        auto init_result = utils::init_loggers(cfg.log_configs, prompt, std_out_mutex, overwrite_default, interactive);
        if (!init_result) {
            spdlog::error("Loggers initialization failed :: {}", init_result.error());
            return 1;
        }

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

        spdlog::info("starting {} {}, libraries: protobuf v{}, lz4: v{}, OpenSSL {}", constants::client_name,
                     constants::client_version, google::protobuf::internal::VersionString(GOOGLE_PROTOBUF_VERSION),
                     LZ4_versionString(), OpenSSL_version(0));

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
                          .fs_config(cfg.fs_config)
                          .finish();

        /* launch actors */
        auto net_thread = std::thread([&]() {
#if defined(__linux__)
            pthread_setname_np(pthread_self(), "synspirit/net");
#endif
            io_context.run();
            console::shutdown_flag = true;
            spdlog::trace("net thread has been terminated");
        });

        auto fs_thread = std::thread([&]() {
#if defined(__linux__)
            pthread_setname_np(pthread_self(), "synspirit/fs");
#endif
            fs_context.run();
            console::shutdown_flag = true;
            spdlog::trace("fs thread has been terminated");
        });
        fs_thread.native_handle();

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
            .interactive(interactive)
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
