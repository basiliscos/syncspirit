// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#pragma once

#include <rotor/thread.hpp>
#include "syncspirit-export.h"
#include "platform/context.h"

namespace syncspirit::fs {

namespace r = rotor;

struct SYNCSPIRIT_API fs_context_t : platform::context_t {
    using parent_t = platform::context_t;
    using parent_t::parent_t;

    void run() noexcept override;
    using parent_t::notify;
};

} // namespace syncspirit::fs
