#pragma once

#include "cluster/unknown_folders.h"

namespace syncspirit::model::diff {

struct cluster_diff_visitor_t {
    virtual void operator()(const cluster::unknown_folders_t &) noexcept;
};

} // namespace syncspirit::model::diff
