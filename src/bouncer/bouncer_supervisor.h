// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include "syncspirit-export.h"

#include <rotor/thread.hpp>

namespace syncspirit::bouncer {

namespace r = rotor;
namespace rth = rotor::thread;

struct SYNCSPIRIT_API bouncer_supervisor_t : rth::supervisor_thread_t {
    using parent_t = rth::supervisor_thread_t;
    using parent_t::parent_t;

    void on_start() noexcept override;
};

} // namespace syncspirit::bouncer
