#include "block_diff.h"
#include "../cluster.h"

using namespace syncspirit::model::diff;

auto block_diff_t::visit(block_visitor_t &) const noexcept -> outcome::result<void> {
    return outcome::success();
}

