// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2023 Ivan Baidakou

#pragma once
#include "../aggregate.h"

namespace syncspirit::model::diff::load {

struct SYNCSPIRIT_API load_cluster_t final : aggregate_t {
    template <typename T> load_cluster_t(T &&diffs_) noexcept : aggregate_t(std::forward<T>(diffs_)) {}

    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;
};

} // namespace syncspirit::model::diff::load
