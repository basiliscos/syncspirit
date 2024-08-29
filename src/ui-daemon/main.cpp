// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include <google/protobuf/stubs/common.h>
#include <lz4.h>
#include <openssl/crypto.h>
#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>
#include <rotor/asio.hpp>
#include <rotor/thread.hpp>
#include <spdlog/spdlog.h>
#include <fstream>
#include <exception>

#include "syncspirit-config.h"
#include "constants.h"
#include "config/utils.h"
#include "utils/location.h"
#include "utils/log.h"
#include "utils/platform.h"
#include "net/net_supervisor.h"
#include "fs/fs_supervisor.h"
#include "hasher/hasher_supervisor.h"
#include "command.h"
#include "governor_actor.h"

#if defined(__linux__)
#include <pthread.h>
#include <signal.h>
#endif

#ifdef _WIN32
#include <windows.h>
#endif

namespace bfs = boost::filesystem;
namespace po = boost::program_options;
namespace pt = boost::posix_time;
namespace r = rotor;
namespace ra = r::asio;
namespace rth = r::thread;
namespace asio = boost::asio;

using namespace syncspirit;
using namespace syncspirit::daemon;

[[noreturn]] static void report_error_and_die(r::actor_base_t *actor, const r::extended_error_ptr_t &ec) noexcept {
    auto name = actor ? actor->get_identity() : "unknown";
    spdlog::critical("actor '{}' error: {}", name, ec->message());
    std::terminate();
}

struct asio_sys_context_t : ra::system_context_asio_t {
    using parent_t = ra::system_context_asio_t;
    using parent_t::parent_t;
    void on_error(r::actor_base_t *actor, const r::extended_error_ptr_t &ec) noexcept override {
        report_error_and_die(actor, ec);
    }
};

struct thread_sys_context_t : rth::system_context_thread_t {
    using parent_t = rth::system_context_thread_t;
    using parent_t::parent_t;
    void on_error(r::actor_base_t *actor, const r::extended_error_ptr_t &ec) noexcept override {
        report_error_and_die(actor, ec);
    }
};

static std::atomic_bool shutdown_flag = false;

#ifdef _WIN32
BOOL WINAPI consoleHandler(DWORD signal) {
    if (signal == CTRL_C_EVENT) {
        shutdown_flag = true;
    }
    return TRUE; /* ignore */
}
#endif

int main(int argc, char **argv) {
    GOOGLE_PROTOBUF_VERIFY_VERSION;

#if defined(__linux__)
    struct sigaction act;
    memset(&act, 0, sizeof(act));
    act.sa_handler = [](int) { shutdown_flag = true; };
    if (sigaction(SIGINT, &act, nullptr) != 0) {
        spdlog::critical("cannot set signal handler");
        return 1;
    }
#endif
#ifdef _WIN32
    if (!SetConsoleCtrlHandler(consoleHandler, true)) {
        spdlog::critical("ERROR: Could not set control handler");
        return -1;
    }
#endif
    try {
        utils::platform_t::startup();

#if defined(__linux__)
        pthread_setname_np(pthread_self(), "ss/main");
#endif
        assert(0 && "TODO: create sequencer");
        // clang-format off
        /* parse command-line & config options */
        po::options_description cmdline_descr("Allowed options");
        using CommandStrings = std::vector<std::string>;
        cmdline_descr.add_options()
            ("help", "show this help message")
            ("log_level", po::value<std::string>()->default_value("info"),
                        "initial log level")
            ("config_dir", po::value<std::string>(),
                        "configuration directory path")
            ("command", po::value<CommandStrings>(), "command for a deamon. "
                "An arg can be:\n"
                "  add_peer:${label}:${device_id} - records peer in a database;"
                    " tries to connect to it and  allows incoming connections"
                    " from peer device;\n"
                "  add_folder:label=${folder_label}:id=${folder_id}:path=${path}"
                    " adds a folder into the database, scans folder there"
                    " (and adds files from it into the database). The"
                    " ${folder_label} is arbitrary human-readable local label,"
                    " while {folder_id} is predefined folder id to be shared"
                    " with other devices. The ${path} is local path to the"
                    " folder, e.g. /storage/music;\n"
                "  share:folder=${label}:device=${device} shares the folder"
                    " with the device. The ${label} can be a folder label or"
                    " folder id, and the device can be a device id, device"
                    " label or the first part of the device id, e.g. for device"
                    " id 'XBOWTOU-Y7H6RM6-D7WT3UB-7P2DZ5G-R6GNZG6-T5CCG54-SGVF3U5-LBM7RQB'"
                    " the first part is 'XBOWTOU';\n"
                "  inactivate:${seconds} shut down daemon after ${seconds}"
                    " of inactivity;\n"
            );
        // clang-format on

        po::variables_map vm;
        po::store(po::parse_command_line(argc, argv, cmdline_descr), vm);
        po::notify(vm);

        bool show_help = vm.count("help");
        if (show_help) {
            std::cout << cmdline_descr << "\n";
            return 1;
        }

        utils::set_default(vm["log_level"].as<std::string>());

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
        auto config_file_path_str = config_file_path.string();
        bool populate = !bfs::exists(config_file_path);
        if (populate) {
            spdlog::info("Config {} seems does not exit, creating default one...", config_file_path.string());
            auto cfg_opt = config::generate_config(config_file_path);
            if (!cfg_opt) {
                spdlog::error("cannot generate default config: {}", cfg_opt.error().message());
                return 1;
            }
            auto &cfg = cfg_opt.value();
            std::fstream f_cfg(config_file_path_str, f_cfg.binary | f_cfg.trunc | f_cfg.in | f_cfg.out);
            auto r = config::serialize(cfg, f_cfg);
            if (!r) {
                spdlog::error("cannot save default config at {}: {}", config_file_path_str, r.error().message());
                return 1;
            }
        }
        std::ifstream config_file(config_file_path_str);
        if (!config_file) {
            spdlog::error("Cannot open config file {}", config_file_path_str);
            return 1;
        }

        config::config_result_t cfg_option = config::get_config(config_file, config_file_path.parent_path());
        if (!cfg_option) {
            spdlog::error("Config file {} is incorrect :: {}", config_file_path_str, cfg_option.error());
            return 1;
        }
        auto &cfg = cfg_option.value();
        spdlog::trace("configuration seems OK");

        bool overwrite_default = vm.count("log_level");
        auto init_result = utils::init_loggers(cfg.log_configs, overwrite_default);
        if (!init_result) {
            spdlog::error("Loggers initialization failed :: {}", init_result.error().message());
            return 1;
        }

        Commands commands;
        if (vm.count("command")) {
            auto cmds = vm["command"].as<CommandStrings>();
            for (auto &cmd : cmds) {
                auto r = command_t::parse(cmd);
                if (!r) {
                    spdlog::error("error parsing {} : {}", cmd, r.assume_error().message());
                    return 1;
                }
                commands.emplace_back(std::move(r.assume_value()));
            }
        }

        if (populate) {
            spdlog::info("Generating cryptographic keys...");
            auto pair = utils::generate_pair(constants::issuer_name);
            if (!pair) {
                spdlog::error("cannot generate cryptographic keys :: {}", pair.error().message());
                return 1;
            }
            auto &keys = pair.value();
            auto &cert_path = cfg.global_announce_config.cert_file;
            auto &key_path = cfg.global_announce_config.key_file;
            auto save_result = keys.save(cert_path.c_str(), key_path.c_str());
            if (!save_result) {
                spdlog::error("cannot store cryptographic keys ({} & {}) :: {}", cert_path, key_path,
                              save_result.error().message());
                return 1;
            }
        }

        spdlog::info("starting {} {}, libraries: protobuf v{}, lz4: v{}, OpenSSL {}", constants::client_name,
                     SYNCSPIRIT_VERSION, google::protobuf::internal::VersionString(GOOGLE_PROTOBUF_VERSION),
                     LZ4_versionString(), OpenSSL_version(0));

        /* pre-init actors */
        asio::io_context io_context;
        ra::system_context_ptr_t sys_context{new asio_sys_context_t{io_context}};
        auto strand = std::make_shared<asio::io_context::strand>(io_context);
        auto timeout = pt::milliseconds{cfg.timeout};

        auto cluster_copies = 1ul;

        auto sup_net = sys_context->create_supervisor<net::net_supervisor_t>()
                           .app_config(cfg)
                           .strand(strand)
                           .timeout(timeout)
                           .create_registry()
                           .guard_context(true)
                           .cluster_copies(cluster_copies)
                           .shutdown_flag(shutdown_flag, r::pt::millisec{50})
                           .finish();
        sup_net->start();
        // pre-startup
        sup_net->do_process();

        thread_sys_context_t fs_context;
        auto fs_sup = fs_context.create_supervisor<syncspirit::fs::fs_supervisor_t>()
                          .timeout(timeout)
                          .registry_address(sup_net->get_registry_address())
                          .fs_config(cfg.fs_config)
                          .hasher_threads(cfg.hasher_threads)
                          .finish();

        // auxiliary payload
        fs_sup->add_launcher([&](model::cluster_ptr_t &cluster) mutable {
            fs_sup->create_actor<governor_actor_t>()
                .commands(std::move(commands))
                .cluster(cluster)
                .timeout(timeout)
                .autoshutdown_supervisor()
                .finish();
        });

        auto hasher_count = cfg.hasher_threads;
        using sys_thread_context_ptr_t = r::intrusive_ptr_t<thread_sys_context_t>;
        std::vector<sys_thread_context_ptr_t> hasher_ctxs;
        for (uint32_t i = 1; i <= hasher_count; ++i) {
            hasher_ctxs.push_back(new thread_sys_context_t{});
            auto &ctx = hasher_ctxs.back();
            ctx->create_supervisor<hasher::hasher_supervisor_t>()
                .timeout(timeout / 2)
                .registry_address(sup_net->get_registry_address())
                .index(i)
                .finish();
        }

        /* launch actors */
        auto fs_thread = std::thread([&]() {
#if defined(__linux__)
            pthread_setname_np(pthread_self(), "ss/fs");
#endif
            fs_context.run();
            shutdown_flag = true;
            spdlog::trace("fs thread has been terminated");
        });

        auto hasher_threads = std::vector<std::thread>();
        for (uint32_t i = 0; i < hasher_count; ++i) {
            auto &ctx = hasher_ctxs.at(i);
            auto thread = std::thread([ctx = ctx, i = i]() {
#if defined(__linux__)
                std::string name = "ss/hasher-" + std::to_string(i + 1);
                pthread_setname_np(pthread_self(), name.c_str());
#endif
                ctx->run();
                shutdown_flag = true;
#if defined(__linux__)
                spdlog::trace("{} thread has been terminated", name);
#endif
            });
            hasher_threads.emplace_back(std::move(thread));
        }

        // main loop;
#if defined(__linux__)
        pthread_setname_np(pthread_self(), "ss/net");
#endif
        io_context.run();
        shutdown_flag = true;
        spdlog::trace("net thread has been terminated");

        spdlog::trace("waiting fs thread termination");
        fs_thread.join();

        spdlog::trace("waiting hasher threads termination");
        for (auto &thread : hasher_threads) {
            thread.join();
        }
        spdlog::trace("everything has been terminated");
    } catch (...) {
        spdlog::critical("unknown exception");
        // spdlog::critical("Starting failure : {}", ex.what());
        return 1;
    }

    utils::platform_t::shutdown();
    google::protobuf::ShutdownProtobufLibrary();
    /* exit */

    spdlog::info("normal exit");
    spdlog::drop_all();
    return 0;
}
