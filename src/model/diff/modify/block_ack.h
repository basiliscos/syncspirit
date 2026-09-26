// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2026 Ivan Baidakou

#pragma once

#include "../block_diff.h"
#include "block_transaction.h"

namespace syncspirit::model::diff::modify {

struct SYNCSPIRIT_API block_ack_t final : block_diff_t {
    using parent_t = block_diff_t;

    block_ack_t(const block_transaction_t &) noexcept;
    block_ack_t(std::string file_name, std::string folder_id, utils::bytes_t device_id, utils::bytes_t block_hash,
                std::uint32_t block_index, bool unlock_block) noexcept;

    using parent_t::parent_t;

    outcome::result<void> apply_impl(apply_controller_t &, void *) const noexcept override;
    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;

    bool unlock_block = false;
};

} // namespace syncspirit::model::diff::modify
