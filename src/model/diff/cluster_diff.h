#pragma once

#include "base.h"
#include <boost/outcome.hpp>

namespace syncspirit::model {

namespace outcome = boost::outcome_v2;

struct cluster_t;

namespace diff {

struct cluster_diff_visitor_t;

struct cluster_diff_t : base_t {
    virtual outcome::result<void> apply(cluster_t &) const noexcept = 0;
    virtual outcome::result<void> visit(cluster_diff_visitor_t &) const noexcept;
};

using cluster_diff_ptr_t = boost::intrusive_ptr<cluster_diff_t>;

} // namespace diff

} // namespace syncspirit::model
