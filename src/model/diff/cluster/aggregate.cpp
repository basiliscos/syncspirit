#include "aggregate.h"

using namespace syncspirit::model::diff::cluster;

auto aggregate_t::apply(cluster_t &cluster) const noexcept -> outcome::result<void> {
    for (auto &diff : diffs) {
        auto r = diff->apply(cluster);
        if (!r) {
            return r;
        }
    }
    return outcome::success();
}

auto aggregate_t::visit(diff_visitor_t &vistor) const noexcept -> outcome::result<void> {
    for (auto &diff : diffs) {
        auto r = diff->visit(vistor);
        if (!r) {
            return r;
        }
    }
    return outcome::success();
}
