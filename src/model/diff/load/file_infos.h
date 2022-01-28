#pragma once

#include "common.h"

namespace syncspirit::model::diff::load {

struct file_infos_t final : cluster_diff_t {
    template <typename T> file_infos_t(T &&container_) noexcept : container{std::forward<T>(container_)} {}

    outcome::result<void> apply_impl(cluster_t &) const noexcept override;

    container_t container;
};

} // namespace syncspirit::model::diff::load
