#include "load_cluster.h"
#include "model/diff/diff_visitor.h"

using namespace syncspirit::model::diff::load;

auto load_cluster_t::visit(diff_visitor_t &visitor) const noexcept -> outcome::result<void> {
    return visitor(*this);
}
