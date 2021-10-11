#include "aggregate.h"

using namespace syncspirit::model::diff::cluster;

void aggregate_t::apply(cluster_t &cluster) const noexcept {
    for (auto &diff : diffs) {
        diff->apply(cluster);
    }
}

void aggregate_t::visit(cluster_diff_visitor_t &vistor) const noexcept {
    for (auto &diff : diffs) {
        diff->visit(vistor);
    }
}
