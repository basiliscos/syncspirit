#include "base_diff.h"
#include "../cluster.h"

using namespace syncspirit::model::diff;

base_diff_t::base_diff_t() noexcept {
    log = utils::get_logger("model");
}

auto base_diff_t::apply(cluster_t &cluster) const noexcept -> outcome::result<void> {
    auto r = apply_impl(cluster);
    if (!r) {
        cluster.mark_tainted();
    }
    return r;
}


