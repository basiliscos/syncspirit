// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "clone_block.h"
#include "model/diff/cluster_visitor.h"
#include "model/cluster.h"
#include "utils/format.hpp"

using namespace syncspirit::model::diff::modify;

clone_block_t::clone_block_t(const file_block_t &file_block, const folder_info_t &target_fi,
                             const folder_info_t &source_fi) noexcept
    : block_transaction_t{*file_block.file(), target_fi, file_block.block_index()} {
    const file_info_t *source_file = nullptr;
    auto it = file_block.block()->iterate_blocks();
    while (auto b = it.next()) {
        if (b->is_locally_available()) {
            source_file = b->file();
            source_block_index = b->block_index();
            break;
        }
    }
    assert(source_file);

    auto folder = source_fi.get_folder();
    source_device_id = source_fi.get_device()->device_id().get_sha256();
    source_folder_id = folder->get_id();
    source_file_name = source_file->get_name()->get_full_name();

    assert(file_block.file()->iterate_blocks(block_index).next()->get_hash() == folder->get_cluster()
                                                                                    ->get_folders()
                                                                                    .by_id(source_folder_id)
                                                                                    ->get_folder_infos()
                                                                                    .by_device_id(source_device_id)
                                                                                    ->get_file_infos()
                                                                                    .by_name(source_file_name)
                                                                                    ->iterate_blocks(source_block_index)
                                                                                    .next()
                                                                                    ->get_hash());
    LOG_DEBUG(log, "clone_block_t, to: file '{}' # {} from bolder {}; source '{}' #{} from folder '{}')",
              *file_block.file(), folder_id, block_index, *source_file, source_block_index, source_folder_id);
}

auto clone_block_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting clone_block_t");
    return visitor(*this, custom);
}
