#pragma once

#include "base_diff.h"

namespace syncspirit::model::diff {

namespace tag {

struct cluster{};
struct block{};

}

template <typename Tag>
struct generic_visitor_t;

template <typename Tag>
struct generic_diff_t : base_diff_t {
    using visitor_t = generic_visitor_t<Tag>;

    virtual outcome::result<void> visit(visitor_t &) const noexcept = 0;
};

}
