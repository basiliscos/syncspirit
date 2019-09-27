#include <chrono>
#include <csignal>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>

#include <boost/filesystem.hpp>

#include "configuration.h"
#include "utils/location.h"
#include "net/net_supervisor.h"
#include <boost/program_options.hpp>
#include <rotor/asio.hpp>
#include <spdlog/spdlog.h>

namespace fs = boost::filesystem;
namespace po = boost::program_options;
namespace pt = boost::posix_time;
namespace ra = rotor::asio;
namespace asio = boost::asio;

using namespace syncspirit;

static std::atomic<bool> signal_shutdown_flag{false};

int main(int argc, char **argv) {
    try {
        // clang-format off
        /* parse command-line & config options */
        po::options_description cmdline_descr("Allowed options");
        cmdline_descr.add_options()
                ("help", "show this help message")
                ("log_level", po::value<std::string>()->default_value("info"), "initial log level")
                ( "config_dir", po::value<std::string>(), "configuration directory path");
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
        auto log_level = config::get_log_level(log_level_str);
        spdlog::set_level(log_level);

        fs::path config_file_path;
        if (vm.count("config_dir")) {
            auto path = vm["config_dir"].as<std::string>();
            config_file_path = fs::path{path.c_str()};
        } else {
            auto config_default = utils::get_default_config_dir();
            if (config_default) {
                config_file_path = config_default.value();
            } else {
                spdlog::error("cannot determine default config dir: {}", config_default.error().message());
                return 1;
            }
        }
        config_file_path.append("syncspirit.conf");
        auto config_file_path_c = config_file_path.c_str();
        std::ifstream config_file(config_file_path_c);
        if (!config_file) {
            spdlog::error("Cannot open config file {}", config_file_path_c);
            return 1;
        }

        auto cfg_option = config::get_config(config_file);
        if (!cfg_option) {
            spdlog::error("Config file {} is incorrect", config_file_path_c);
            return 1;
        }
        spdlog::trace("configuration seems OK");
        spdlog::info("starting");

        /* pre-init actors */
        asio::io_context io_context;
        ra::system_context_ptr_t sys_context{new ra::system_context_asio_t{io_context}};
        auto stand = std::make_shared<asio::io_context::strand>(io_context);
        auto timeout = pt::milliseconds{1000};
        ra::supervisor_config_asio_t sup_conf{timeout, std::move(stand)};
        auto sup_net = sys_context->create_supervisor<net::net_supervisor_t>(sup_conf, *cfg_option);
        sup_net->start();

        /* launch actors */
        auto net_thread = std::thread([&io_context, &sup_net]() {
            io_context.run();
            spdlog::trace("net thread has been terminated");
            signal_shutdown_flag = true;
        });

        struct sigaction act;
        act.sa_handler = [](int) { signal_shutdown_flag = true; };
        if (sigaction(SIGINT, &act, nullptr) != 0) {
            spdlog::critical("cannot set signal handler");
            return 1;
        }
        auto console_thread = std::thread([] {
            while (!signal_shutdown_flag) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            spdlog::trace("console thread has been terminated");
        });

        spdlog::trace("waiting actors terminations");
        console_thread.join();

        sup_net->shutdown();
        net_thread.join();
        spdlog::trace("everything has been terminated");
    } catch (const std::exception &ex) {
        spdlog::critical("Starting failure : {}", ex.what());
        return 1;
    }

    /* exit */
    spdlog::info("normal exit");
    return 0;
}
