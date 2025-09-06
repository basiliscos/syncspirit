// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include "utils/log.h"
#include "syncspirit-export.h"
#include "messages.h"

#include <rotor/thread.hpp>

namespace syncspirit::hasher {

namespace r = rotor;
namespace rth = rotor::thread;

struct SYNCSPIRIT_API bouncer_actor_t : rth::supervisor_thread_t {
    using parent_t = rth::supervisor_thread_t;
    using parent_t::parent_t;

    void configure(r::plugin::plugin_base_t &plugin) noexcept override;

  private:
    void on_package(message::package_t &message) noexcept;

    utils::logger_t log;
};

} // namespace syncspirit::hasher
