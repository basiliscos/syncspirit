// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include <lz4.h>
#include <openssl/crypto.h>
#include <filesystem>
#include <boost/program_options.hpp>
#include <boost/nowide/convert.hpp>
#include <rotor/asio.hpp>
#include <rotor/thread.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <exception>

#include "syncspirit-config.h"
#include "constants.h"
#include "config/utils.h"
#include "utils/io.h"
#include "utils/location.h"
#include "utils/log-setup.h"
#include "utils/platform.h"
#include "net/net_supervisor.h"
#include "fs/fs_context.h"
#include "fs/fs_supervisor.h"
#include "bouncer/bouncer_actor.h"
#include "hasher/hasher_supervisor.h"
#include "command.h"
#include "governor_actor.h"

#if defined(__unix__)
#include <signal.h>
#endif

#if defined(__linux__)
#include <pthread.h>
#endif

#ifdef _WIN32
#include <windows.h>
#include <winnls.h>
#endif

namespace bfs = std::filesystem;
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
    utils::get_root_logger()->critical("actor '{}' error: {}", name, ec->message());
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

struct fs_context_t : fs::fs_context_t {
    using parent_t = fs::fs_context_t;
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

struct app_context_t {
    int argc;
    char **argv;
    utils::logger_t::element_type *logger;
    utils::dist_sink_t &dist_sink;
    utils::bootstrap_guard_ptr_t &bootstrap_guard;
};

int app_main(app_context_t &ctx);

int main(int argc, char **argv) {
    auto bootstrap_guard = utils::bootstrap_guard_ptr_t();
    if (!utils::platform_t::startup()) {
        fprintf(stderr, "cannot startup platform\n");
        return 1;
    }

#if defined(__unix__) || defined(__APPLE__) || defined(__MACH__)
    struct sigaction act;
    memset(&act, 0, sizeof(act));
    act.sa_handler = [](int) { shutdown_flag = true; };
    if (sigaction(SIGINT, &act, nullptr) != 0) {
        fprintf(stderr, "cannot set signal handler\n");
        return 1;
    }
#endif

#ifdef _WIN32
    if (!SetConsoleCtrlHandler(consoleHandler, true)) {
        fprintf(stderr, "Could not set control handler\n");
        return -1;
    }
#endif

    auto [dist_sink, logger] = utils::create_root_logger();
    dist_sink->add_sink(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
    logger->trace("root logger has been bootstrapped");

    try {
        app_context_t ctx(argc, argv, logger.get(), dist_sink, bootstrap_guard);
        app_main(ctx);
    } catch (std::exception &ex) {
        logger->critical("app failure : {}", ex.what());
    } catch (...) {
        logger->critical("unknown exception");
        return 1;
    }

    utils::platform_t::shutdown();

    logger->info("normal exit");
    bootstrap_guard.reset();
    utils::finalize_loggers();
    return 0;
}

int app_main(app_context_t &app_ctx) {
#if defined(__linux__)
    pthread_setname_np(pthread_self(), "ss/main");
#endif
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
    po::store(po::parse_command_line(app_ctx.argc, app_ctx.argv, cmdline_descr), vm);
    po::notify(vm);

    bool show_help = vm.count("help");
    if (show_help) {
        std::cout << cmdline_descr << "\n";
        return 1;
    }

    auto log_level = utils::get_log_level(vm["log_level"].as<std::string>());
    auto logger = app_ctx.logger;

    bfs::path config_file_path;
    if (vm.count("config_dir")) {
        auto path = vm["config_dir"].as<std::string>();
        config_file_path = bfs::path{path.c_str()};
    } else {
        auto config_default = utils::get_default_config_dir();
        if (config_default) {
            config_file_path = config_default.value();
        } else {
            logger->error("cannot determine default config dir: {}", config_default.error().message());
            return 1;
        }
    }
    app_ctx.bootstrap_guard = utils::bootstrap(app_ctx.dist_sink, config_file_path);

    config_file_path.append("syncspirit.toml");
    auto config_file_path_str = config_file_path.string();
    bool populate = !bfs::exists(config_file_path);
    if (populate) {
        logger->info("Config {} seems does not exit, creating default one...", config_file_path.string());
        auto cfg_opt = config::generate_config(config_file_path);
        if (!cfg_opt) {
            logger->error("cannot generate default config: {}", cfg_opt.error().message());
            return 1;
        }
        auto &cfg = cfg_opt.value();
        using F = utils::fstream_t;
        auto f_cfg = utils::fstream_t(config_file_path, F::binary | F::trunc | F::in | F::out);
        auto r = config::serialize(cfg, f_cfg);
        if (!r) {
            logger->error("cannot save default config at {}: {}", config_file_path_str, r.error().message());
            return 1;
        }
    }
    auto config_file = utils::ifstream_t(config_file_path);
    if (!config_file) {
        logger->error("Cannot open config file {}", config_file_path_str);
        return 1;
    }

    config::config_result_t cfg_option = config::get_config(config_file, config_file_path.parent_path());
    if (!cfg_option) {
        logger->error("Config file {} is incorrect :: {}", config_file_path_str, cfg_option.error());
        return 1;
    }
    auto &cfg = cfg_option.value();
    logger->trace("configuration seems OK, timeout = {}ms", cfg.timeout);
    auto poll_timeout = r::pt::milliseconds{cfg.poll_timeout};

    // override default
    if (log_level) {
        auto value = int(log_level.value());
        logger->trace("overriding default log level to {}", value);
        app_ctx.logger->set_level(log_level.value());
    }
    auto init_result = utils::init_loggers(cfg.log_configs);
    if (!init_result) {
        logger->error("Loggers initialization failed :: {}", init_result.error().message());
        return 1;
    }

    Commands commands;
    if (vm.count("command")) {
        auto cmds = vm["command"].as<CommandStrings>();
        for (auto &cmd : cmds) {
            auto r = command_t::parse(cmd);
            if (!r) {
                logger->error("error parsing {} : {}", cmd, r.assume_error().message());
                return 1;
            }
            commands.emplace_back(std::move(r.assume_value()));
        }
    }

    {
        auto &cert_path = cfg.cert_file;
        auto &key_path = cfg.key_file;
        auto ec = std::error_code{};
        if (!bfs::exists(cert_path, ec) || !bfs::exists(key_path, ec)) {
            auto cert_path_str = boost::nowide::narrow(cert_path.wstring());
            auto key_path_str = boost::nowide::narrow(key_path.wstring());
            logger->trace("'{}' or '{}' do not exist", cert_path_str, key_path_str);
            logger->info("Generating cryptographic keys...");
            auto pair = utils::generate_pair(constants::issuer_name);
            if (!pair) {
                logger->error("cannot generate cryptographic keys :: {}", pair.error().message());
                return 1;
            }
            auto &keys = pair.value();
            auto save_result = keys.save(cert_path_str.c_str(), key_path_str.c_str());
            if (!save_result) {
                logger->error("cannot store cryptographic keys ({} & {}) :: {}", cert_path, key_path,
                              save_result.error().message());
                return 1;
            }
        }
    }

    logger->info("starting {} {}", constants::client_name, SYNCSPIRIT_VERSION);

    /* pre-init actors */
    asio::io_context io_context;
    ra::system_context_ptr_t sys_context{new asio_sys_context_t{io_context}};
    auto strand = std::make_shared<asio::io_context::strand>(io_context);
    auto timeout = pt::milliseconds{cfg.timeout};
    auto independent_threads = 1ul;
    auto seed = (size_t)std::time(nullptr);
    auto sequencer = model::make_sequencer(seed);

    static std::atomic_bool bouncer_shutdown_flag = false;
    auto bouncer_context = thread_sys_context_t();
    auto bouncer_sup = bouncer_context.create_supervisor<r::thread::supervisor_thread_t>()
                           .timeout(timeout * 9 / 8)
                           .shutdown_flag(bouncer_shutdown_flag, r::pt::millisec{50})
                           .finish();
    bouncer_sup->do_process();
    auto bouncer_actor = bouncer_sup->create_actor<bouncer::bouncer_actor_t>().timeout(timeout * 9 / 8).finish();
    bouncer_sup->do_process();

    auto sup_net = sys_context->create_supervisor<net::net_supervisor_t>()
                       .app_config(cfg)
                       .strand(strand)
                       .timeout(timeout)
                       .create_registry()
                       .guard_context(true)
                       .sequencer(sequencer)
                       .independent_threads(independent_threads)
                       .shutdown_flag(shutdown_flag, r::pt::millisec{50})
                       .bouncer_address(bouncer_actor->get_address())
                       .poll_duration(poll_timeout)
                       .finish();

    // auxiliary payload
    sup_net->add_launcher([&](model::cluster_ptr_t &cluster) mutable {
        sup_net->create_actor<governor_actor_t>()
            .commands(std::move(commands))
            .cluster(cluster)
            .sequencer(sequencer)
            .timeout(timeout)
            .autoshutdown_supervisor()
            .finish();
    });

    // warm-up
    sup_net->do_process();
    if (sup_net->get_shutdown_reason()) {
        logger->debug("net supervisor has not started");
        return 1;
    }

    auto bouncer_thread = std::thread([&]() {
#if defined(__linux__)
        pthread_setname_np(pthread_self(), "ss/bouncer");
#endif
        logger->trace("running bouncer");
        bouncer_context.run();
        bouncer_shutdown_flag = true;
        logger->trace("bouncer thread has been terminated");
    });

    auto hasher_count = cfg.hasher_threads;
    using sys_thread_context_ptr_t = r::intrusive_ptr_t<thread_sys_context_t>;
    std::vector<sys_thread_context_ptr_t> hasher_ctxs;
    for (uint32_t i = 1; i <= hasher_count; ++i) {
        hasher_ctxs.push_back(new thread_sys_context_t{});
        auto &ctx = hasher_ctxs.back();
        auto sup = ctx->create_supervisor<hasher::hasher_supervisor_t>()
                       .poll_duration(poll_timeout)
                       .timeout(timeout * 8 / 9)
                       .registry_address(sup_net->get_registry_address())
                       .index(i)
                       .finish();
        sup->do_process();
    }

    auto fs_context = fs_context_t(pt::milliseconds{cfg.fs_config.poll_timeout});
    auto fs_sup = fs_context.create_supervisor<syncspirit::fs::fs_supervisor_t>()
                      .shutdown_flag(shutdown_flag, r::pt::millisec{50})
                      .timeout(timeout)
                      .poll_duration(poll_timeout)
                      .registry_address(sup_net->get_registry_address())
                      .fs_config(cfg.fs_config)
                      .hasher_threads(cfg.hasher_threads)
                      .finish();
    fs_sup->do_process();
    /* launch actors */

    auto hasher_threads = std::vector<std::thread>();
    for (uint32_t i = 0; i < hasher_count; ++i) {
        auto &ctx = hasher_ctxs.at(i);
        auto thread = std::thread([ctx = ctx, i = i, logger]() {
#if defined(__linux__)
            std::string name = "ss/hasher-" + std::to_string(i + 1);
            pthread_setname_np(pthread_self(), name.c_str());
#endif
            ctx->run();
            shutdown_flag = true;
#if defined(__linux__)
            logger->trace("{} thread has been terminated", name);
#endif
        });
        hasher_threads.emplace_back(std::move(thread));
    }

    auto fs_thread = std::thread([&]() {
#if defined(__linux__)
        pthread_setname_np(pthread_self(), "ss/fs");
#endif
        fs_context.run();
        shutdown_flag = true;
        logger->trace("fs thread has been terminated");
    });

    // main loop;
#if defined(__linux__)
    pthread_setname_np(pthread_self(), "ss/net");
#endif
    sup_net->do_process();
    io_context.run();
    shutdown_flag = true;
    logger->trace("net thread has been terminated");

    logger->trace("joining to fs thread");
    fs_thread.join();

    logger->trace("joining to {} hasher threads", hasher_threads.size());
    for (auto &thread : hasher_threads) {
        thread.join();
    }

    logger->trace("joining to bouncer thread");
    bouncer_shutdown_flag = true;
    bouncer_thread.join();

    if (auto reason = sup_net->get_shutdown_reason(); reason && reason->ec) {
        logger->info("app shut down reason: {}", reason->message());
    }

    logger->trace("everything has been terminated");

    return 0;
}
