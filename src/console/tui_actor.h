#pragma once

#include "../configuration.h"
#include "../ui/messages.hpp"
#include <rotor/asio.hpp>
#include <boost/asio.hpp>
#include <optional>
#include <atomic>
#include <fmt/fmt.h>

namespace syncspirit {
namespace console {

namespace r = rotor;
namespace ra = rotor::asio;
namespace asio = boost::asio;
namespace sys = boost::system;

struct tui_actor_config_t : r::actor_config_t {
    std::mutex *mutex;
    std::string *prompt;
    std::atomic_bool *shutdown;
    config::tui_config_t tui_config;
};

template <typename Actor> struct tui_actor_config_builder_t : r::actor_config_builder_t<Actor> {
    using builder_t = typename Actor::template config_builder_t<Actor>;
    using parent_t = r::actor_config_builder_t<Actor>;
    using parent_t::parent_t;

    builder_t &&mutex(std::mutex *value) &&noexcept {
        parent_t::config.mutex = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&prompt(std::string *value) &&noexcept {
        parent_t::config.prompt = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&shutdown(std::atomic_bool *value) &&noexcept {
        parent_t::config.shutdown = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&tui_config(const config::tui_config_t &value) &&noexcept {
        parent_t::config.tui_config = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
};

struct tui_actor_t : public r::actor_base_t {
    using config_t = tui_actor_config_t;
    template <typename Actor> using config_builder_t = tui_actor_config_builder_t<Actor>;

    explicit tui_actor_t(config_t &cfg);

    void on_start() noexcept override;
    void shutdown_start() noexcept override;
    void configure(r::plugin::plugin_base_t &plugin) noexcept override;

  private:
    void on_discovery(ui::message::discovery_notify_t &message) noexcept;
    void start_timer() noexcept;
    void on_timer(r::request_id_t, bool cancelled) noexcept;
    void do_read() noexcept;
    void on_read(size_t bytes) noexcept;
    void on_read_error(const sys::error_code &ec) noexcept;
    void action_quit() noexcept;
    void action_help() noexcept;
    void action_more_logs() noexcept;
    void action_less_logs() noexcept;
    void action_esc() noexcept;
    void set_prompt(const std::string &value) noexcept;
    void reset_prompt() noexcept;
    void flush_prompt() noexcept;

    using tty_t = std::unique_ptr<asio::posix::stream_descriptor>;

    asio::io_context::strand &strand;
    tty_t tty;
    size_t progress_idx = 0;
    size_t progress_last;
    static const char *progress;
    std::optional<r::request_id_t> timer_id;
    std::mutex *mutex;
    std::string *prompt;
    std::string prompt_buff;
    std::atomic_bool *shutdown_flag;
    config::tui_config_t tui_config;
    r::address_ptr_t coordinator;
    r::address_ptr_t controller;
    char input[2];
    char progress_symbol;
};

} // namespace console
} // namespace syncspirit
