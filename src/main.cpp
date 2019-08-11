#include <chrono>
#include <csignal>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>

#include "configuration.h"
#include "net/net_supervisor.h"
#include <boost/program_options.hpp>
#include <rotor/asio.hpp>
#include <spdlog/spdlog.h>

namespace po = boost::program_options;
namespace pt = boost::posix_time;
namespace ra = rotor::asio;
namespace asio = boost::asio;

using namespace syncspirit;

static std::atomic<bool> signal_shutdown_flag{false};

int main(int argc, char **argv) {
    using guard_t = asio::executor_work_guard<asio::io_context::executor_type>;
    try {
        // clang-format off
        /* parse command-line & config options */
        po::options_description cmdline_descr("Allowed options");
        cmdline_descr.add_options()
                ("help", "show this help message")
                ("log_level", po::value<std::string>()->default_value("info"), "initial log level")
                ( "config_file", po::value<std::string>()->default_value("syncspirit.conf"), "configuration file path");
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

        std::string config_file_path = vm["config_file"].as<std::string>();
        std::ifstream config_file(config_file_path.c_str());
        if (!config_file) {
            spdlog::error("Cannot open config file {}", config_file_path);
            return 1;
        }

        auto cfg_option = config::get_config(config_file);
        if (!cfg_option) {
            spdlog::error("Config file {} is incorrect", config_file_path);
            return 1;
        }
        spdlog::trace("configuration seems OK");
        spdlog::info("starting");

        /* pre-init actors */
        asio::io_context io_context;
        ra::system_context_ptr_t sys_context{new ra::system_context_asio_t{io_context}};
        ra::supervisor_config_t sup_conf{pt::milliseconds{500}};
        auto sup_net = sys_context->create_supervisor<net::net_supervisor_t>(sup_conf, *cfg_option);
        sup_net->start();

        /* launch actors */
        guard_t net_guard{asio::make_work_guard(io_context)};
        auto net_thread = std::thread([&io_context]() {
            io_context.run();
            spdlog::trace("net thread has been terminated");
            signal_shutdown_flag = true;
        });

        struct sigaction act;
        act.sa_handler = [](int) { signal_shutdown_flag = true; };
        if (sigaction(SIGINT, &act, nullptr) != 0) {
            spdlog::critical("cannot set signal handler");
        }
        auto console_thread = std::thread([] {
            while (!signal_shutdown_flag) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            spdlog::trace("console thread has been terminated");
        });

        spdlog::trace("waiting actors terminations");
        console_thread.join();
        net_guard.reset();

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
