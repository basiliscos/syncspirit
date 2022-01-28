#pragma once

#include "../cluster_diff.h"
#include "structs.pb.h"

namespace syncspirit::model::diff::modify {

struct create_folder_t final : cluster_diff_t {

    template <typename T> create_folder_t(T &&item_) noexcept : item{std::forward<T>(item_)} {}

    outcome::result<void> apply_impl(cluster_t &) const noexcept override;
    outcome::result<void> visit(cluster_visitor_t &) const noexcept override;

    db::Folder item;
};

} // namespace syncspirit::model::diff::modify
