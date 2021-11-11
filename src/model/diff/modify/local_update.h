#pragma once

#include <optional>
#include "bep.pb.h"
#include "utils/log.h"
#include "../cluster_diff.h"

namespace syncspirit::model::diff::modify {

struct content_change_t {
    std::vector<std::string> blocks;
    std::string symlink_target;
    proto::FileInfoType file_type;
    size_t file_size;
    size_t block_size;
};

struct local_update_t final : cluster_diff_t {
    local_update_t(std::string_view folder_id, std::string_view file_name, content_change_t current,
                   std::optional<content_change_t> prev, bool deleted, bool invalid) noexcept;

    outcome::result<void> apply_impl(cluster_t &) const noexcept override;
    outcome::result<void> visit(diff_visitor_t &) const noexcept override;

    std::string folder_id;
    std::string file_name;
    std::optional<content_change_t> prev;
    content_change_t current;
    bool deleted;
    bool invalid;
};

}
