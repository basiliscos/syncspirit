// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include "../block_diff.h"

namespace syncspirit::model::diff::modify {

struct SYNCSPIRIT_API block_transaction_t : block_diff_t {
    using parent_t = block_diff_t;

    block_transaction_t(const file_info_t &file, size_t block_index);

    outcome::result<void> apply_impl(cluster_t &, apply_controller_t &) const noexcept override;

    cluster_diff_ptr_t ack() const;
    cluster_diff_ptr_t rej() const;
};

} // namespace syncspirit::model::diff::modify
