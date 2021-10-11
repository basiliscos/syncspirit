#pragma once

#include <vector>
#include "bep.pb.h"
#include "../cluster_diff.h"

namespace syncspirit::model::diff::cluster {

struct unknown_folders_t final : cluster_diff_t {
    using folders_t = std::vector<proto::Folder>;

    template <typename T> unknown_folders_t(T &&folders_) noexcept : folders(std::forward<T>(folders)) {}

    void apply(cluster_t &) const noexcept override;
    void visit(cluster_diff_visitor_t &) const noexcept override;

    folders_t folders;
};

} // namespace syncspirit::model::diff::cluster
