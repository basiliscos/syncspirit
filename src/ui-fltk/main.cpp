// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#include <lz4.h>
#include <openssl/crypto.h>
#include <filesystem>
#include <boost/program_options.hpp>
#include <rotor/asio.hpp>
#include <rotor/thread.hpp>
#include <spdlog/spdlog.h>
#include <exception>
#include <cstdlib>
#include <spdlog/sinks/stdout_color_sinks.h>

#include "syncspirit-config.h"
#include "constants.h"
#include "config/utils.h"
#include "utils/io.h"
#include "utils/location.h"
#include "utils/log-setup.h"
#include "utils/platform.h"
#include "hasher/hasher_supervisor.h"
#include "net/net_supervisor.h"
#include "fs/fs_supervisor.h"

#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Box.H>
#include <FL/fl_utf8.h>

#include "app_supervisor.h"
#include "main_window.h"
#include "log_sink.h"

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
namespace rf = rotor::fltk;
namespace asio = boost::asio;

using namespace syncspirit;

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

struct fltk_context_t : rf::system_context_fltk_t {
    using parent_t = rf::system_context_fltk_t;
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

#ifdef _WIN32
#define SET_THREAD_EN_LANGUAGE() SetThreadUILanguage(MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US))
#else
#define SET_THREAD_EN_LANGUAGE()
#endif

int main(int argc, char **argv) {
    auto bootstrap_guard = utils::bootstrap_guard_ptr_t();

    Fl::lock();

#if defined(__unix__) || defined(__APPLE__) || defined(__MACH__)
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
        SET_THREAD_EN_LANGUAGE();
        utils::platform_t::startup();

        auto dist_sink = utils::create_root_logger();
        dist_sink->add_sink(spdlog::sink_ptr(new fltk::im_memory_sink_t()));
        if (auto value = std::getenv(constants::console_sink_env); value && value == std::string_view("1")) {
            auto console_sink = spdlog::sink_ptr(new spdlog::sinks::stderr_color_sink_mt());
            dist_sink->add_sink(console_sink);
        }
        spdlog::trace("root logger has been bootstrapped");

        Fl::args(1, argv);

#if defined(__linux__)
        pthread_setname_np(pthread_self(), "ss/main");
#endif
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

        auto log_level = utils::get_log_level(vm["log_level"].as<std::string>());
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
        bootstrap_guard = utils::bootstrap(dist_sink, config_file_path);

        config_file_path.append("syncspirit.toml");
        bool populate = !bfs::exists(config_file_path);
        if (populate) {
            spdlog::info("config {} seems does not exit, creating default one...", config_file_path.string());
            auto cfg_opt = config::generate_config(config_file_path);
            if (!cfg_opt) {
                spdlog::error("cannot generate default config: {}", cfg_opt.error().message());
                return 1;
            }
            auto &cfg = cfg_opt.value();
            using F = utils::fstream_t;
            auto f_cfg = utils::fstream_t(config_file_path, F::binary | F::trunc | F::in | F::out);
            auto r = config::serialize(cfg, f_cfg);
            if (!r) {
                spdlog::error("cannot save default config at :: {}", r.error().message());
                return 1;
            }
        }
        auto config_file = utils::ifstream_t(config_file_path);
        if (!config_file) {
            spdlog::error("Cannot open config file {}", config_file_path.string());
            return 1;
        }

        config::config_result_t cfg_option = config::get_config(config_file, config_file_path.parent_path());
        if (!cfg_option) {
            spdlog::error("Config file {} is incorrect: {}", config_file_path.string(), cfg_option.error());
            return 1;
        }
        auto &cfg = cfg_option.value();
        spdlog::trace("configuration seems OK, timeout = {}ms", cfg.timeout);

        // override default
        if (log_level) {
            if (cfg.log_configs.size()) {
                auto &first_cfg = cfg.log_configs.front();
                if (first_cfg.name == "default") {
                    auto level = log_level.value();
                    first_cfg.level = level;
                    spdlog::trace("overriding default log level to {}", int(level));
                }
            }
        }
        auto init_result = utils::init_loggers(cfg.log_configs);
        if (!init_result) {
            spdlog::error("loggers initialization failed :: {}", init_result.error().message());
            return 1;
        }

        if (populate) {
            spdlog::info("generating cryptographic keys...");
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

        asio::io_context io_context;
        ra::system_context_ptr_t sys_context{new asio_sys_context_t{io_context}};
        auto strand = std::make_shared<asio::io_context::strand>(io_context);
        auto timeout = pt::milliseconds{cfg.timeout};
        auto independent_threads = 3ul;
        auto seed = (size_t)std::time(nullptr);
        auto sequencer = model::make_sequencer(seed);

        auto sup_net = sys_context->create_supervisor<net::net_supervisor_t>()
                           .app_config(cfg)
                           .strand(strand)
                           .timeout(timeout)
                           .create_registry()
                           .guard_context(true)
                           .sequencer(sequencer)
                           .independent_threads(independent_threads)
                           .shutdown_flag(shutdown_flag, r::pt::millisec{50})
                           .finish();
        // warm-up
        sup_net->do_process();
        if (sup_net->get_shutdown_reason()) {
            spdlog::debug("net supervisor has not started");
            return 1;
        }

        auto hasher_count = cfg.hasher_threads;
        using sys_thread_context_ptr_t = r::intrusive_ptr_t<thread_sys_context_t>;
        std::vector<sys_thread_context_ptr_t> hasher_ctxs;
        for (uint32_t i = 1; i <= hasher_count; ++i) {
            hasher_ctxs.push_back(new thread_sys_context_t{});
            auto &ctx = hasher_ctxs.back();
            auto sup = ctx->create_supervisor<hasher::hasher_supervisor_t>()
                           .timeout(timeout / 2)
                           .registry_address(sup_net->get_registry_address())
                           .index(i)
                           .finish();
            sup->do_process();
        }

        // window should outlive fltk ctx, as in ctx d-tor model augmentations
        // invoke fltk-things..
        auto main_window = std::unique_ptr<fltk::main_window_t>();
        auto fltk_ctx = rf::system_context_ptr_t(new fltk_context_t());
        auto sup_fltk = fltk_ctx->create_supervisor<fltk::app_supervisor_t>()
                            .dist_sink(bootstrap_guard->get_dist_sink())
                            .config_path(config_file_path)
                            .app_config(cfg)
                            .timeout(timeout)
                            .registry_address(sup_net->get_registry_address())
                            .shutdown_flag(shutdown_flag, r::pt::millisec{50})
                            .finish();
        // warm-up
        sup_fltk->do_process();

        thread_sys_context_t fs_context;
        auto fs_sup = fs_context.create_supervisor<syncspirit::fs::fs_supervisor_t>()
                          .timeout(timeout)
                          .registry_address(sup_net->get_registry_address())
                          .fs_config(cfg.fs_config)
                          .hasher_threads(cfg.hasher_threads)
                          .sequencer(sequencer)
                          .finish();
        fs_sup->do_process();

        auto app_w = cfg.fltk_config.main_window_width;
        auto app_h = cfg.fltk_config.main_window_height;
        main_window.reset(new fltk::main_window_t(*sup_fltk, app_w, app_h));
        Fl::visual(FL_DOUBLE | FL_INDEX);
        main_window->show(argc, argv);
        main_window->wait_for_expose();

        // launch
        auto net_thread = std::thread([&]() {
            SET_THREAD_EN_LANGUAGE();
#if defined(__linux__)
            std::string name = "ss/net";
            pthread_setname_np(pthread_self(), name.c_str());
#endif
            io_context.run();
            shutdown_flag = true;
            spdlog::trace("net thread has been terminated");
        });

        auto hasher_threads = std::vector<std::thread>();
        for (uint32_t i = 0; i < hasher_count; ++i) {
            auto &ctx = hasher_ctxs.at(i);
            auto thread = std::thread([ctx = ctx, i = i]() {
                SET_THREAD_EN_LANGUAGE();
                std::string name = "ss/hasher-" + std::to_string(i + 1);
#if defined(__linux__)
                pthread_setname_np(pthread_self(), name.c_str());
#endif
                ctx->run();
                shutdown_flag = true;
                spdlog::trace("{} thread has been terminated", name);
            });
            hasher_threads.emplace_back(std::move(thread));
        }

        auto fs_thread = std::thread([&]() {
            SET_THREAD_EN_LANGUAGE();
#if defined(__linux__)
            pthread_setname_np(pthread_self(), "ss/fs");
#endif
            fs_context.run();
            shutdown_flag = true;
            spdlog::trace("fs thread has been terminated");
        });

        spdlog::debug("utf8 local support: {}", fl_utf8locale());

        while (!shutdown_flag) {
            sup_fltk->do_process();
            if (!Fl::wait()) {
                shutdown_flag = true;
                spdlog::debug("main window is longer show, terminating...");
                break;
            }
        }

        sup_fltk->do_shutdown();
        sup_fltk->do_process();

        fs_thread.join();
        net_thread.join();

        spdlog::trace("waiting hasher threads termination");
        for (auto &thread : hasher_threads) {
            thread.join();
        }

        if (auto reason = sup_net->get_shutdown_reason(); reason && reason->ec) {
            spdlog::info("app shut down reason: {}", reason->message());
        }

        spdlog::trace("everything has been terminated");
        sup_fltk.reset();
        fltk_ctx.reset();
        main_window.reset();
        spdlog::trace("fltk context has been destroyed");
    } catch (...) {
        spdlog::critical("unknown exception");
        // spdlog::critical("Starting failure : {}", ex.what());
        return 1;
    }

    utils::platform_t::shutdown();
    /* exit */

    spdlog::info("normal exit");
    bootstrap_guard.reset();
    utils::finalize_loggers();
    return 0;
}
