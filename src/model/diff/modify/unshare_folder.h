// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2023 Ivan Baidakou

#pragma once

#include <set>
#include "../cluster_diff.h"
#include "utils/string_comparator.hpp"

namespace syncspirit::model::diff::modify {

struct SYNCSPIRIT_API unshare_folder_t final : cluster_diff_t {
    using keys_t = std::set<std::string, utils::string_comparator_t>;

    unshare_folder_t(const model::cluster_t &cluster, std::string_view peer_device_,
                     std::string_view folder_id_) noexcept;

    outcome::result<void> apply_impl(cluster_t &) const noexcept override;
    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;

    std::string peer_id;
    std::string folder_id;
    std::string folder_info_key;
    keys_t removed_files;
    keys_t removed_blocks;
};

} // namespace syncspirit::model::diff::modify
