#include "remove_corrupted_files.h"
#include "model/diff/cluster_visitor.h"

using namespace syncspirit::model::diff::load;

auto remove_corrupted_files_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    return visitor(*this, custom);
}
