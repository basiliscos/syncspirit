// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025-2026 Ivan Baidakou

#pragma once

#include "hash_base.h"

namespace syncspirit::net::local_keeper {

struct hash_incomplete_file_t : hash_base_t {
    inline hash_incomplete_file_t(child_info_t info_, model::file_info_t &file, model::advance_action_t action_)
        : hash_base_t(std::move(info_), false) {
        incomplete = true;
        block_size = file.get_block_size();
        unprocessed_blocks = unhashed_blocks = total_blocks = file.iterate_blocks().get_total();
        blocks.resize(total_blocks);
        self = &file;
        action = action_;
    }
};

} // namespace syncspirit::net::local_keeper
