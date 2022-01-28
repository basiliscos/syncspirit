#pragma once

#include "../cluster_diff.h"
#include "model/file_info.h"

namespace syncspirit::model::diff::modify {

struct flush_file_t final : cluster_diff_t {
    flush_file_t(const model::file_info_t &file) noexcept;

    outcome::result<void> apply_impl(cluster_t &) const noexcept override;
    outcome::result<void> visit(cluster_visitor_t &) const noexcept override;

    std::string folder_id;
    std::string device_id;
    std::string file_name;
};

} // namespace syncspirit::model::diff::modify
