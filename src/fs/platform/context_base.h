// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#pragma once

#include "syncspirit-export.h"

#include <rotor/thread.hpp>
#include <cstdint>
#include "utils/log.h"

namespace syncspirit::fs::platform {

namespace rth = rotor::thread;
namespace pt = rotor::pt;

struct SYNCSPIRIT_API context_base_t : rth::system_context_thread_t {
    using parent_t = rth::system_context_thread_t;
    context_base_t(const pt::time_duration &poll_timeout) noexcept;

    std::uint32_t determine_wait_ms() noexcept;

    pt::time_duration poll_timeout;
    int poll_timeout_ms;
    utils::logger_t log;
};

} // namespace syncspirit::fs::platform
