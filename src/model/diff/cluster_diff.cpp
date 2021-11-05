#include "cluster_diff.h"
#include "../cluster.h"

using namespace syncspirit::model::diff;

cluster_diff_t::~cluster_diff_t() {

}

auto cluster_diff_t::apply(cluster_t &cluster) const noexcept -> outcome::result<void> {
    auto r = apply_impl(cluster);
    if (!r) {
        cluster.mark_tainted();
    }
    return r;
}


auto cluster_diff_t::visit(diff_visitor_t &) const noexcept -> outcome::result<void> {
    return outcome::success();
}
