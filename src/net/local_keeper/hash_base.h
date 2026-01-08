// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include "child_info.h"
#include "model/misc/resolver.h"
#include <cstdint>

namespace syncspirit::net::local_keeper {

struct hash_base_t : model::arc_base_t<hash_base_t>, child_info_t {
    using blocks_t = std::vector<proto::BlockInfo>;

    hash_base_t(child_info_t &&info_, std::int32_t block_size_ = 0);
    bool commit_error(sys::error_code ec_, std::int32_t delta);
    bool commit_hash() const;

    std::int32_t block_size;
    std::int32_t total_blocks;
    std::int32_t unprocessed_blocks;
    std::int32_t unhashed_blocks;
    std::int32_t errored_blocks = 0;
    sys::error_code ec;
    blocks_t blocks;
    model::advance_action_t action = model::advance_action_t::ignore;
    bool incomplete = false;
};

} // namespace syncspirit::net::local_keeper
