#pragma once

#include "../block_diff.h"
#include "model/file_info.h"

namespace syncspirit::model::diff::modify {

struct blocks_availability_t final : block_diff_t {

    blocks_availability_t(const file_info_t& file, size_t last_block_index) noexcept;

    outcome::result<void> apply_impl(cluster_t &) const noexcept override;
    outcome::result<void> visit(block_visitor_t &) const noexcept override;

    proto::Vector version;
};

}
