#pragma once

#include "../config/main.h"
#include "../ui/messages.hpp"
#include "../model/device_id.h"
#include "activity.h"
#include <rotor/asio.hpp>
#include <boost/asio.hpp>
#include <fmt/fmt.h>
#include <optional>
#include <atomic>
#include <memory>
#include <list>

namespace syncspirit {
namespace console {

namespace r = rotor;
namespace ra = rotor::asio;
namespace asio = boost::asio;
namespace sys = boost::system;

struct tui_actor_config_t : r::actor_config_t {
    std::mutex *mutex;
    std::string *prompt;
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

    using tty_t = std::unique_ptr<asio::posix::stream_descriptor>;
    using activity_ptr_t = std::unique_ptr<activity_t>;
    using activities_t = std::list<activity_ptr_t>;

    void on_discovery(ui::message::discovery_notify_t &message) noexcept;
    void on_auth(ui::message::auth_notify_t &message) noexcept;
    void on_config(ui::message::config_response_t &message) noexcept;
    void on_config_save(ui::message::config_save_response_t &message) noexcept;
    void on_new_folder(ui::message::new_folder_notify_t &message) noexcept;
    void on_create_folder(ui::message::create_folder_response_t &message) noexcept;

    void start_timer() noexcept;
    void on_timer(r::request_id_t, bool cancelled) noexcept;
    void do_read() noexcept;
    void on_read(size_t bytes) noexcept;
    void on_read_error(const sys::error_code &ec) noexcept;
    void action_quit() noexcept;
    void action_more_logs() noexcept;
    void action_less_logs() noexcept;
    void action_esc() noexcept;
    void action_config() noexcept;
    void save_config() noexcept;
    void set_prompt(const std::string &value) noexcept;
    void flush_prompt() noexcept;
    void push_activity(activity_ptr_t &&activity) noexcept;
    void postpone_activity() noexcept;
    void discard_activity() noexcept;
    void ignore_device(const model::device_id_t &) noexcept;
    void ignore_folder(const proto::Folder &folder, model::device_ptr_t &source) noexcept;
    void create_folder(const proto::Folder &folder, model::device_ptr_t &source) noexcept;

    asio::io_context::strand &strand;
    config::main_t app_config;
    config::main_t app_config_orig;
    tty_t tty;
    size_t progress_idx = 0;
    size_t progress_last;
    static const char *progress;
    std::optional<r::request_id_t> timer_id;
    std::mutex *mutex;
    std::string *prompt;
    std::string prompt_buff;
    config::tui_config_t tui_config;
    r::address_ptr_t coordinator;
    char input[2];
    char progress_symbol;
    activities_t activities;
    size_t activities_count = 0;
};

} // namespace console
} // namespace syncspirit
