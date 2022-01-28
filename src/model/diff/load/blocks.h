#pragma once

#include "common.h"

namespace syncspirit::model::diff::load {

struct blocks_t final : cluster_diff_t {
    template <typename T> blocks_t(T &&blocks_) noexcept : blocks{std::forward<T>(blocks_)} {}

    outcome::result<void> apply_impl(cluster_t &) const noexcept override;

    container_t blocks;
};

} // namespace syncspirit::model::diff::load
