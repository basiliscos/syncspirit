#include "cluster_diff.h"
#include "../cluster.h"

using namespace syncspirit::model::diff;

auto cluster_diff_t::visit(cluster_visitor_t &) const noexcept -> outcome::result<void> { return outcome::success(); }
