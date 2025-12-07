// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include "child_info.h"

namespace syncspirit::net::local_keeper {

struct child_ready_t : child_info_t {
    inline child_ready_t(child_info_t info, blocks_t blocks_ = {})
        : child_info_t{std::move(info)}, blocks{std::move(blocks_)} {}
    blocks_t blocks;
};

} // namespace syncspirit::net::local_keeper
