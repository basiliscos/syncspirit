#pragma once

#include "../block_diff.h"
#include "model/file_info.h"

namespace syncspirit::model::diff::modify {

struct clone_block_t final : block_diff_t {
    clone_block_t(const file_info_t& target_file, const block_info_t& block) noexcept;

    outcome::result<void> apply_impl(cluster_t &) const noexcept override;
    outcome::result<void> visit(block_visitor_t &) const noexcept override;

    std::string target_folder_id;
    std::string target_file_name;
    size_t target_block_index;

    std::string source_folder_id;
    std::string source_file_name;
    size_t source_block_index;
};

}
