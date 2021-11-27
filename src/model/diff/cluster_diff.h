#pragma once

#include "generic_diff.hpp"
#include "cluster_visitor.h"

namespace syncspirit::model::diff {

struct cluster_diff_t: generic_diff_t<tag::cluster> {
    virtual outcome::result<void> visit(cluster_visitor_t &) const noexcept override;
};

using cluster_diff_ptr_t = boost::intrusive_ptr<cluster_diff_t>;


} // namespace syncspirit::model
