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

#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Box.H>

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


namespace {
namespace to {
struct on_timer_trigger {};
struct timers_map {};
} // namespace to
} // namespace


namespace rotor {
template <>
inline auto rotor::actor_base_t::access<to::on_timer_trigger, request_id_t, bool>(request_id_t request_id,
                                                                                  bool cancelled) noexcept {
    on_timer_trigger(request_id, cancelled);
}
}

namespace rotor::fltk {

using system_context_fltk_t = rotor::system_context_t;

using system_context_ptr_t = rotor::intrusive_ptr_t<system_context_fltk_t>;

using supervisor_config_fltk_t  = supervisor_config_t;

template <typename Supervisor>
using supervisor_config_fltk_builder_t = supervisor_config_builder_t<Supervisor>;

struct supervisor_fltk_t : public supervisor_t {

    using config_t = supervisor_config_fltk_t;

    /** \brief injects templated supervisor_config_wx_builder_t */
    template <typename Supervisor> using config_builder_t = supervisor_config_fltk_builder_t<Supervisor>;

    /** \brief constructs new supervisor from supervisor config */
    supervisor_fltk_t(supervisor_config_fltk_t &config);

    void start() noexcept override;
    void shutdown() noexcept override;
    void enqueue(message_ptr_t message) noexcept override;

    template <typename T> auto &access() noexcept;

    struct timer_t {
        using supervisor_ptr_t = intrusive_ptr_t<supervisor_fltk_t>;

        timer_t(supervisor_ptr_t supervisor, timer_handler_base_t *handler);
        timer_t(timer_t&) = delete;
        timer_t(timer_t&&) = default;

        timer_handler_base_t *handler;
        supervisor_ptr_t sup;
    };

protected:
    using timer_ptr_t = std::unique_ptr<timer_t>;
    using timers_map_t = std::unordered_map<request_id_t, timer_ptr_t>;

    void do_start_timer(const pt::time_duration &interval, timer_handler_base_t &handler) noexcept override;
    void do_cancel_timer(request_id_t timer_id) noexcept override;

    timers_map_t timers_map;
};

template <> inline auto &supervisor_fltk_t::access<to::timers_map>() noexcept { return timers_map; }

supervisor_fltk_t::supervisor_fltk_t(supervisor_config_fltk_t &config): supervisor_t(config){}

static void on_timeout(void* data) noexcept {
    auto timer = reinterpret_cast<supervisor_fltk_t::timer_t*>(data);
    auto *sup = timer->sup.get();

    auto timer_id = timer->handler->request_id;
    auto &timers_map = sup->access<to::timers_map>();

    try {
        auto actor_ptr = timers_map.at(timer_id)->handler->owner;
        actor_ptr->access<to::on_timer_trigger, request_id_t, bool>(timer_id, false);
        timers_map.erase(timer_id);
        sup->do_process();
    } catch (std::out_of_range &) {
        // no-op
    }
    intrusive_ptr_release(sup);
}

supervisor_fltk_t::timer_t::timer_t(supervisor_ptr_t supervisor_, timer_handler_base_t *handler_):
    sup(std::move(supervisor_)), handler{handler_}{}


void supervisor_fltk_t::do_start_timer(const pt::time_duration &interval, timer_handler_base_t &handler) noexcept {
    auto self = timer_t::supervisor_ptr_t(this);
    auto timer = std::make_unique<timer_t>(std::move(self), &handler);
    auto seconds = interval.total_nanoseconds() / 1000000000.;
    Fl::add_timeout(seconds, on_timeout, timer.get());
    timers_map.emplace(handler.request_id, std::move(timer));
    intrusive_ptr_add_ref(this);
}

void supervisor_fltk_t::do_cancel_timer(request_id_t timer_id) noexcept {
    try {
        auto &timer = timers_map.at(timer_id);
        Fl::remove_timeout(on_timeout, timer.get());
        auto actor_ptr = timer->handler->owner;
        actor_ptr->access<to::on_timer_trigger, request_id_t, bool>(timer_id, true);
        timers_map.erase(timer_id);
        intrusive_ptr_release(this);
    } catch (std::out_of_range &) {
        // no-op
    }
}


void supervisor_fltk_t::enqueue(message_ptr_t message) noexcept {
    struct holder_t {
        message_ptr_t message;
        supervisor_fltk_t* supervisor;
    };
    auto holder = new holder_t(std::move(message), this);
    intrusive_ptr_add_ref(this);
    Fl::awake([](void *data){
        auto holder = reinterpret_cast<holder_t*>(data);
        auto sup = holder->supervisor;
        sup->put(std::move(holder->message));
        sup->do_process();
        intrusive_ptr_release(sup);
        delete holder;
    }, holder); // call to execute cb in main thread
}

void supervisor_fltk_t::start() noexcept {
    intrusive_ptr_add_ref(this);
    Fl::awake([](void *data){
        auto sup = reinterpret_cast<supervisor_fltk_t*>(data);
        sup->do_process();
        intrusive_ptr_release(sup);
    }, this);
}

void supervisor_fltk_t::shutdown() noexcept {
    intrusive_ptr_add_ref(this);
    Fl::awake([](void *data){
        auto sup = reinterpret_cast<supervisor_fltk_t*>(data);
        auto ec = make_error_code(shutdown_code_t::normal);
        auto reason = sup->make_error(ec);
        sup->do_shutdown(reason);
        sup->do_process();
        intrusive_ptr_release(sup);
    }, this);
}

}

namespace rf = rotor::fltk;

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
        // clang-format off
        /* parse command-line & config options */
        po::options_description cmdline_descr("Allowed options");
        using CommandStrings = std::vector<std::string>;
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

        utils::set_default(vm["log_level"].as<std::string>());

        auto timeout = pt::milliseconds{500};

        auto fltk_ctx = rf::system_context_fltk_t();
        auto sup_fltk = fltk_ctx.create_supervisor<rf::supervisor_fltk_t>()
                            .timeout(timeout)
                            .create_registry()
                            .shutdown_flag(shutdown_flag, r::pt::millisec{50})
                            .finish();
        // warm-up
        sup_fltk->do_process();

        Fl_Window window = Fl_Window(300,180);
        Fl_Box *box = new Fl_Box(20,40,260,100,"Hello, World!");
        box->box(FL_UP_BOX);
        box->labelsize(36);
        box->labelfont(FL_BOLD+FL_ITALIC);
        box->labeltype(FL_SHADOW_LABEL);
        window.end();
        window.show(argc, argv);

        while(!shutdown_flag){
            sup_fltk->do_process();
            if (!Fl::wait()) {
                spdlog::debug("main window is longer show, terminating...");
                break;
            }
        }

        sup_fltk->do_shutdown();
        sup_fltk->do_process();

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
