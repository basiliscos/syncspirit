#pragma once

#include "base.h"
#include <boost/outcome.hpp>

namespace syncspirit::model {

namespace outcome = boost::outcome_v2;

struct cluster_t;

namespace diff {

struct diff_visitor_t;

struct cluster_diff_t : base_t {
    virtual ~cluster_diff_t();
    virtual outcome::result<void> visit(diff_visitor_t &) const noexcept;
    outcome::result<void> apply(cluster_t &) const noexcept;

    protected:
    virtual outcome::result<void> apply_impl(cluster_t &) const noexcept = 0;
};

using cluster_diff_ptr_t = boost::intrusive_ptr<cluster_diff_t>;

} // namespace diff

} // namespace syncspirit::model
