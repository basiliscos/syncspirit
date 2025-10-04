// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include "common.h"
#include "proto/proto-fwd.hpp"

namespace syncspirit::model::diff::load {

struct SYNCSPIRIT_API blocks_t final : cluster_diff_t {
    struct item_t {
        utils::bytes_view_t key;
        db::BlockInfo db_block;
    };
    using container_t = std::vector<item_t>;

    template <typename T> blocks_t(T &&blocks_) noexcept : blocks{std::forward<T>(blocks_)} {}

    outcome::result<void> apply_impl(apply_controller_t &, void *) const noexcept override;
    outcome::result<void> apply_forward(apply_controller_t &, void *) const noexcept override;

    container_t blocks;
};

} // namespace syncspirit::model::diff::load
