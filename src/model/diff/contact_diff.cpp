#include "contact_diff.h"
#include "../cluster.h"

using namespace syncspirit::model::diff;

auto contact_diff_t::visit(contact_visitor_t &) const noexcept -> outcome::result<void> {
    return outcome::success();
}

