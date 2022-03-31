// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#include "clone_block.h"
#include "../block_visitor.h"
#include "../../cluster.h"
#include "../../misc/error_code.h"

using namespace syncspirit::model::diff::modify;

clone_block_t::clone_block_t(const file_block_t &file_block) noexcept
    : block_diff_t{*file_block.file(), file_block.block_index()} {
    const file_info_t *source_file = nullptr;
    auto &block_pieces = file_block.block()->get_file_blocks();
    for (auto &b : block_pieces) {
        if (b.is_locally_available()) {
            source_file = b.file();
            source_block_index = b.block_index();
            break;
        }
    }
    assert(source_file);
    assert(file_block.file()->get_blocks().at(block_index)->get_hash() ==
           block_pieces[source_block_index].block()->get_hash());
    auto source_fi = source_file->get_folder_info();
    source_device_id = source_fi->get_device()->device_id().get_sha256();
    source_folder_id = source_fi->get_folder()->get_id();
    source_file_name = source_file->get_name();
}

auto clone_block_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    auto target_folder = cluster.get_folders().by_id(folder_id);
    auto target_folder_info = target_folder->get_folder_infos().by_device_id(device_id);
    auto target_file = target_folder_info->get_file_infos().by_name(file_name);
    target_file->mark_local_available(block_index);
    return outcome::success();
}

auto clone_block_t::visit(block_visitor_t &visitor) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting clone_block_t");
    return visitor(*this);
}
