#pragma once

#include "../cluster_diff.h"
#include "bep.pb.h"

namespace syncspirit::model::diff::modify {

struct new_file_t final : cluster_diff_t {
    using blocks_t = std::vector<proto::BlockInfo>;

    new_file_t(std::string_view folder_id, proto::FileInfo file, bool inc_sequence, blocks_t blocks) noexcept;

    outcome::result<void> apply_impl(cluster_t &) const noexcept override;
    outcome::result<void> visit(diff_visitor_t &) const noexcept override;

    std::string folder_id;
    proto::FileInfo file;
    blocks_t blocks;
    bool inc_sequence;
};

}
