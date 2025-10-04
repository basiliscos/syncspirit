// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include "utils/log.h"
#include "syncspirit-export.h"
#include "messages.hpp"

#include <rotor/request.hpp>

namespace syncspirit::bouncer {

namespace r = rotor;

struct SYNCSPIRIT_API bouncer_actor_t : r::actor_base_t {
    using parent_t = r::actor_base_t;
    using parent_t::parent_t;

    void configure(r::plugin::plugin_base_t &plugin) noexcept override;
    void shutdown_start() noexcept override;
    void shutdown_finish() noexcept override;

  private:
    void on_package(message::package_t &message) noexcept;

    utils::logger_t log;
};

} // namespace syncspirit::bouncer
