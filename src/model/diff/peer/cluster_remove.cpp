// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2023 Ivan Baidakou

#include "cluster_remove.h"
#include "model/diff/cluster_visitor.h"

using namespace syncspirit::model::diff::peer;

cluster_remove_t::cluster_remove_t(std::string_view source_device_, keys_t removed_folder_infos_, keys_t removed_files_,
                                   keys_t removed_blocks_, keys_t removed_unknown_folders_) noexcept
    : source_device{source_device_}, removed_folder_infos{std::move(removed_folder_infos_)},
      removed_files{std::move(removed_files_)}, removed_blocks{std::move(removed_blocks_)},
      removed_unknown_folders{std::move(removed_unknown_folders_)} {}

auto cluster_remove_t::apply_impl(cluster_t &) const noexcept -> outcome::result<void> { return outcome::success(); }

auto cluster_remove_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    return visitor(*this, custom);
}
