#pragma once

#include "generic_diff.hpp"
#include "block_visitor.h"

namespace syncspirit::model {

struct file_info_t;

namespace diff {

struct block_diff_t: generic_diff_t<tag::block> {
    block_diff_t(const file_info_t& file, size_t block_index = 0) noexcept;
    virtual outcome::result<void> visit(block_visitor_t &) const noexcept override;

    std::string file_name;
    std::string folder_id;
    std::string device_id;
    size_t block_index;
};

using block_diff_ptr_t = boost::intrusive_ptr<block_diff_t>;

}
}
