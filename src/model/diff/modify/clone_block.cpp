// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "clone_block.h"
#include "model/diff/cluster_visitor.h"
#include "model/cluster.h"
#include "utils/format.hpp"

using namespace syncspirit::model::diff::modify;

clone_block_t::clone_block_t(const file_block_t &file_block) noexcept
    : block_transaction_t{*file_block.file(), file_block.block_index()} {
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

    auto source_fi = source_file->get_folder_info();
    source_device_id = source_fi->get_device()->device_id().get_sha256();
    source_folder_id = source_fi->get_folder()->get_id();
    source_file_name = source_file->get_name()->get_full_name();

    assert(file_block.file()->get_blocks().at(block_index)->get_hash() == source_fi->get_folder()
                                                                              ->get_cluster()
                                                                              ->get_folders()
                                                                              .by_id(source_fi->get_folder()->get_id())
                                                                              ->get_folder_infos()
                                                                              .by_device(*source_fi->get_device())
                                                                              ->get_file_infos()
                                                                              .by_name(source_file_name)
                                                                              ->get_blocks()
                                                                              .at(source_block_index)
                                                                              ->get_hash());
    LOG_DEBUG(log, "clone_block_t, file: '{}', block: {} (source '{}' #{})", *file_block.file(), block_index,
              *source_file, source_block_index);
}

auto clone_block_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting clone_block_t");
    return visitor(*this, custom);
}
