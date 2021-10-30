#pragma once

#include <vector>
#include "../cluster_diff.h"

namespace syncspirit::model::diff::cluster {

struct aggregate_t final : cluster_diff_t {
    using diffs_t = std::vector<cluster_diff_ptr_t>;

    template <typename T> aggregate_t(T &&diffs_) noexcept : diffs(std::forward<T>(diffs_)) {}

    outcome::result<void> apply(cluster_t &) const noexcept override;
    outcome::result<void> visit(cluster_diff_visitor_t &) const noexcept override;

    diffs_t diffs;
};

} // namespace syncspirit::model::diff::cluster
