// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#pragma once

#include "syncspirit-export.h"

#include <rotor/thread.hpp>
#include <cstdint>
#include "utils/log.h"

namespace syncspirit::fs::platform {

namespace rth = rotor::thread;

struct SYNCSPIRIT_API context_base_t : rth::system_context_thread_t {
    using parent_t = rth::system_context_thread_t;
    using parent_t::parent_t;

    context_base_t() noexcept;

    std::uint32_t determine_wait_ms() noexcept;

    utils::logger_t log;
};

} // namespace syncspirit::fs::platform
