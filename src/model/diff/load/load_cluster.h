#include "../aggregate.h"

namespace syncspirit::model::diff::load {

struct load_cluster_t final : aggregate_t {
    template <typename T> load_cluster_t(T &&diffs_) noexcept : aggregate_t(std::forward<T>(diffs_)) {}

    outcome::result<void> visit(cluster_visitor_t &) const noexcept override;
};

} // namespace syncspirit::model::diff::load
