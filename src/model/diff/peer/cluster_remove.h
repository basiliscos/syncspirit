// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2023 Ivan Baidakou

#pragma once

#include <set>
#include "../cluster_diff.h"
#include "model/device.h"
#include "model/folder_info.h"
#include "proto/bep_support.h"
#include "utils/string_comparator.hpp"

namespace syncspirit::model::diff::peer {

struct SYNCSPIRIT_API cluster_remove_t final : cluster_diff_t {
    using keys_t = std::set<std::string, utils::string_comparator_t>;

    cluster_remove_t(std::string_view source_device, keys_t removed_folder_infos_, keys_t removed_files_,
                     keys_t removed_blocks_, keys_t removed_unknown_folders_) noexcept;

    outcome::result<void> apply_impl(cluster_t &) const noexcept override;
    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;

    std::string source_device;
    keys_t removed_folder_infos;
    keys_t removed_files;
    keys_t removed_blocks;
    keys_t removed_unknown_folders;
};

} // namespace syncspirit::model::diff::peer
