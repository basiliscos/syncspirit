#pragma once

#include "../cluster_diff.h"
#include "model/file_info.h"
#include "bep.pb.h"

namespace syncspirit::model::diff::modify {

struct clone_file_t final : cluster_diff_t {
    using blocks_t = std::vector<proto::BlockInfo>;
    using new_blocks_t = std::vector<size_t>;

    clone_file_t(const model::file_info_t &source) noexcept;

    outcome::result<void> apply_impl(cluster_t &) const noexcept override;
    outcome::result<void> visit(cluster_visitor_t &) const noexcept override;

    std::string folder_id;
    std::string device_id;
    std::string peer_id;
    proto::FileInfo file;
    bool has_blocks;
    bool create_new_file;
    bool identical;
};

} // namespace syncspirit::model::diff::modify
