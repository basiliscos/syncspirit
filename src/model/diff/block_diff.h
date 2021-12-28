#pragma once

#include "generic_diff.hpp"
#include "block_visitor.h"

namespace syncspirit::model::diff {

struct block_diff_t: generic_diff_t<tag::block> {
    block_diff_t(std::string_view folder_id, std::string_view file_name, size_t block_index = 0) noexcept;
    virtual outcome::result<void> visit(block_visitor_t &) const noexcept override;

    std::string folder_id;
    std::string file_name;
    size_t block_index;
};

using block_diff_ptr_t = boost::intrusive_ptr<block_diff_t>;

}
