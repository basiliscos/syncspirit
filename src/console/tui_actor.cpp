#include "tui_actor.h"
#include "sink.h"
#include <spdlog/spdlog.h>
#include "../net/names.h"

using namespace syncspirit::console;

namespace {
namespace resource {
r::plugin::resource_id_t tty = 0;
}
} // namespace

const char *tui_actor_t::progress = "|/-\\";

tui_actor_t::tui_actor_t(config_t &cfg)
    : r::actor_base_t{cfg}, strand{static_cast<ra::supervisor_asio_t *>(cfg.supervisor)->get_strand()},
      mutex{cfg.mutex}, prompt{cfg.prompt}, shutdown_flag{cfg.shutdown}, tui_config{cfg.tui_config} {

    progress_last = strlen(progress);
    tty = std::make_unique<tty_t::element_type>(strand.context(), STDIN_FILENO);
    reset_prompt();
}

void tui_actor_t::on_start() noexcept {
    spdlog::debug("tui_actor_t::on_start (addr = {})", (void *)address.get());
    r::actor_base_t::on_start();
    start_timer();
    do_read();
}

void tui_actor_t::shutdown_start() noexcept {
    spdlog::debug("tui_actor_t::shutdown_start (addr = {})", (void *)address.get());
    r::actor_base_t::shutdown_start();
    supervisor->do_shutdown();
    send<r::payload::shutdown_trigger_t>(coordinator, coordinator);
    if (resources->has(resource::tty)) {
        tty->cancel();
    }
}

void tui_actor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
        p.discover_name(net::names::coordinator, coordinator, true).link();
        p.discover_name(net::names::controller, controller, true).link().callback([&](auto phase, auto &ec) {
            if (!ec && phase == r::plugin::registry_plugin_t::phase_t::linking) {
                auto p = get_plugin(r::plugin::starter_plugin_t::class_identity);
                auto plugin = static_cast<r::plugin::starter_plugin_t *>(p);
                plugin->subscribe_actor(&tui_actor_t::on_discovery, controller);
            }
        });
    });
}

void tui_actor_t::start_timer() noexcept {
    auto interval = r::pt::milliseconds{tui_config.refresh_interval};
    timer_id = r::actor_base_t::start_timer(interval, *this, &tui_actor_t::on_timer);
}

void tui_actor_t::do_read() noexcept {
    resources->acquire(resource::tty);
    auto fwd = ra::forwarder_t(*this, &tui_actor_t::on_read, &tui_actor_t::on_read_error);
    asio::mutable_buffer buff(input, 1);
    asio::async_read(*tty, buff, std::move(fwd));
}

void tui_actor_t::on_read(size_t) noexcept {
    resources->release(resource::tty);
    input[1] = 0;
    auto k = input[0];
    if (k == tui_config.key_quit) {
        action_quit();
    } else if (k == tui_config.key_help) {
        action_help();
    } else if (k == tui_config.key_more_logs) {
        action_more_logs();
    } else if (k == tui_config.key_less_logs) {
        action_less_logs();
    } else if (k == 27) { /* escape */
        action_esc();
    }
    do_read();
}

void tui_actor_t::on_read_error(const sys::error_code &ec) noexcept {
    resources->release(resource::tty);
    if (ec != asio::error::operation_aborted) {
        spdlog::error("tui_actor_t::on_read_error, stdin reading error :: {}", ec.message());
        do_shutdown();
    }
}

void tui_actor_t::on_timer(r::request_id_t, bool) noexcept {
    if (*shutdown_flag) {
        return do_shutdown();
    }
    flush_prompt();
    start_timer();
}

void tui_actor_t::set_prompt(const std::string &value) noexcept {
    prompt_buff = value;
    flush_prompt();
}

void tui_actor_t::action_quit() noexcept {
    spdlog::info("tui_actor_t::action_quit");
    *shutdown_flag = true;
}

void tui_actor_t::reset_prompt() noexcept {
    auto p = fmt::format("{}{}{}{} - help > ", sink_t::bold, sink_t::white, "?", sink_t::reset);
    set_prompt(std::string(p.begin(), p.end()));
}

void tui_actor_t::action_help() noexcept {
    auto letter = [](char c) -> std::string {
        return fmt::format("{}{}{}{}", sink_t::bold, sink_t::white, std::string_view(&c, 1), sink_t::reset);
    };
    auto key = [](const char *val) -> std::string {
        return fmt::format("{}{}{}{}", sink_t::bold, sink_t::white, val, sink_t::reset);
    };

    auto p = fmt::format("[{}] - quit,  [{}] - more logs, [{}] - less logs, [{}] - back > ", letter('q'), letter('+'),
                         letter('-'), key("ESC"));
    set_prompt(p);
}

void tui_actor_t::action_more_logs() noexcept {
    auto level = spdlog::default_logger_raw()->level();
    auto l = static_cast<int>(level);
    if (l > 0) {
        spdlog::set_level(static_cast<decltype(level)>(--l));
        spdlog::info("tui_actor_t::action_more_logs, applied ({})", l);
    }
}

void tui_actor_t::action_less_logs() noexcept {
    auto level = spdlog::default_logger_raw()->level();
    auto l = static_cast<int>(level);
    auto m = static_cast<int>(decltype(level)::critical);
    if (l < m) {
        spdlog::info("tui_actor_t::action_less_logs, applied ({})", l);
        spdlog::set_level(static_cast<decltype(level)>(++l));
    }
}

void tui_actor_t::action_esc() noexcept { reset_prompt(); }

void tui_actor_t::on_discovery(ui::message::discovery_notify_t &message) noexcept {
    spdlog::critical("tui_actor_t::on_discovery");
}

void tui_actor_t::flush_prompt() noexcept {
    char c;
    if (progress_idx < progress_last) {
        c = progress[progress_idx];
        ++progress_idx;
    } else {
        c = progress[progress_idx = 0];
    }
    auto r = fmt::format("\r\033[2K[{}{}{}{}] {}", sink_t::bold, sink_t::cyan, std::string_view(&c, 1), sink_t::reset,
                         prompt_buff);
    std::lock_guard<std::mutex> lock(*mutex);
    *prompt = r;
    fwrite(prompt->data(), sizeof(char), prompt->size(), stdout);
    fflush(stdout);
}
