// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include "hash_base.h"
#include "presentation/cluster_file_presence.h"

namespace syncspirit::net::local_keeper {

struct hash_incomplete_file_t : hash_base_t {
    inline hash_incomplete_file_t(child_info_t info_, const presentation::presence_t *presence_,
                                  model::advance_action_t action_)
        : hash_base_t(std::move(info_), false) {
        incomplete = true;
        auto cp = static_cast<const presentation::cluster_file_presence_t *>(presence_);
        auto &file = cp->get_file_info();
        block_size = file.get_block_size();
        unprocessed_blocks = unhashed_blocks = total_blocks = file.iterate_blocks().get_total();
        blocks.resize(total_blocks);
        self = const_cast<presentation::presence_t *>(presence_);
        action = action_;
    }
};

} // namespace syncspirit::net::local_keeper
