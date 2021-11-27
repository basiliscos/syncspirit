#pragma once

#include "generic_diff.hpp"
#include "block_visitor.h"

namespace syncspirit::model::diff {

struct block_diff_t: generic_diff_t<tag::block> {
    virtual outcome::result<void> visit(block_visitor_t &) const noexcept override;
};

using block_diff_ptr_t = boost::intrusive_ptr<block_diff_t>;

}
