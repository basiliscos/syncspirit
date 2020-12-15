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
      mutex{cfg.mutex}, prompt{cfg.prompt}, shutdown_flag{cfg.shutdown} {

    progress_last = strlen(progress);
    tty = std::make_unique<tty_t::element_type>(strand.context(), STDIN_FILENO);
}

void tui_actor_t::on_start() noexcept {
    spdlog::debug("tui_actor_t::on_start (addr = {})", (void *)address.get());
    r::actor_base_t::on_start();
    start_timer();
    do_read();
}

void tui_actor_t::shutdown_start() noexcept {
    r::actor_base_t::shutdown_start();
    supervisor->do_shutdown();
    send<r::payload::shutdown_trigger_t>(coordinator, coordinator);
    if (resources->has(resource::tty)) {
        tty->cancel();
    }
}

void tui_actor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::registry_plugin_t>(
        [&](auto &p) { p.discover_name(net::names::coordinator, coordinator, true).link(); });
}

void tui_actor_t::start_timer() noexcept {
    auto interval = r::pt::milliseconds{100};
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
    spdlog::info("console: {}", input);
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
    char c;
    if (progress_idx < progress_last) {
        c = progress[progress_idx];
        ++progress_idx;
    } else {
        c = progress[progress_idx = 0];
    }
    {
        std::lock_guard<std::mutex> lock(*mutex);
        // "[*] >"
        prompt->resize(6);
        auto ptr = prompt->data();
        *ptr++ = '[';
        *ptr++ = c;
        *ptr++ = ']';
        *ptr++ = ' ';
        *ptr++ = '>';
        *ptr++ = ' ';
        fwrite("\r", 1, 1, stdout);
        fwrite(prompt->data(), sizeof(char), prompt->size(), stdout);
        fflush(stdout);
    }
    start_timer();
}
