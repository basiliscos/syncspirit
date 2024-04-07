// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include "../block_diff.h"
#include <atomic>
#include <functional>

namespace syncspirit::model::diff::modify {

struct SYNCSPIRIT_API block_transaction_t : block_diff_t {
    using parent_t = block_diff_t;
    using dispose_callback_t = std::function<void(block_transaction_t &)>;

    block_transaction_t(const file_info_t &file, size_t block_index, dispose_callback_t callback);
    ~block_transaction_t();

    outcome::result<void> apply_impl(cluster_t &) const noexcept override;

    mutable std::atomic_int errors{0};
    dispose_callback_t callback;
};

} // namespace syncspirit::model::diff::modify
