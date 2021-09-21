#pragma once

#include "../config/main.h"
#include "../utils/log.h"
#include "messages.h"
#include <rotor.hpp>

namespace syncspirit {
namespace fs {

struct file_actor_t : public r::actor_base_t {
    using config_t = r::actor_base_t::config_t;

    explicit file_actor_t(config_t &cfg);

    void on_start() noexcept override;
    void configure(r::plugin::plugin_base_t &plugin) noexcept override;

  private:
    void on_open(message::open_request_t &req) noexcept;
    void on_close(message::close_request_t &req) noexcept;
    void on_clone(message::clone_request_t &req) noexcept;

    utils::logger_t log;
};

} // namespace fs
} // namespace syncspirit
