#pragma once

#include "../cluster_diff.h"
#include "bep.pb.h"

namespace syncspirit::model::diff::modify {

struct new_file_t final : cluster_diff_t {
    using blocks_t = std::vector<proto::BlockInfo>;
    using new_blocks_t = std::vector<size_t>;

    new_file_t(const cluster_t &cluster, std::string_view folder_id, proto::FileInfo file, blocks_t blocks) noexcept;

    outcome::result<void> apply_impl(cluster_t &) const noexcept override;
    outcome::result<void> visit(cluster_visitor_t &) const noexcept override;

    std::string folder_id;
    proto::FileInfo file;
    blocks_t blocks;
    new_blocks_t new_blocks;
    bool identical_data;
    bool new_uuid;
};

} // namespace syncspirit::model::diff::modify
