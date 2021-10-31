#include "cluster_diff.h"

using namespace syncspirit::model::diff;

cluster_diff_t::~cluster_diff_t() {

}

auto cluster_diff_t::visit(cluster_diff_visitor_t &) const noexcept -> outcome::result<void> {
    return outcome::success();
}
