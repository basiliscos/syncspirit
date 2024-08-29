// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#include "blocks_availability.h"
#include "../block_visitor.h"
#include "../../cluster.h"
#include "../../misc/error_code.h"
#include "model/misc/version_utils.h"

using namespace syncspirit::model::diff::local;

blocks_availability_t::blocks_availability_t(const file_info_t &file, size_t last_block_index) noexcept
    : block_diff_t{file, last_block_index} {
    version = file.get_version();
    assert(!file.is_locally_available());
}

auto blocks_availability_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    auto folder = cluster.get_folders().by_id(folder_id);
    auto folder_info = folder->get_folder_infos().by_device_id(device_id);
    auto file = folder_info->get_file_infos().by_name(file_name);
    auto ok = file && (compare(version, file->get_version()) == version_relation_t::identity);
    if (ok) {
        for (size_t i = 0; i <= block_index; ++i) {
            file->mark_local_available(i);
        }
    }
    return applicator_t::apply_sibling(cluster);
}

auto blocks_availability_t::visit(block_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting blocks_availability_t");
    return visitor(*this, custom);
}
