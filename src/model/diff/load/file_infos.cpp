// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "file_infos.h"
#include "model/cluster.h"
#include "model/diff/apply_controller.h"
#include "proto/proto-helpers-db.h"
#include <unordered_map>

using namespace syncspirit::model::diff::load;

auto file_infos_t::apply_forward(apply_controller_t &controller, void *custom) const noexcept -> outcome::result<void> {
    return controller.apply(*this, custom);
}

auto file_infos_t::apply_impl(apply_controller_t &controller, void *custom) const noexcept -> outcome::result<void> {
    using folder_info_by_id_t = std::unordered_map<utils::bytes_view_t, folder_info_ptr_t>;
    auto all_fi = folder_info_by_id_t{};
    auto &cluster = controller.get_cluster();
    auto &folders = cluster.get_folders();
    for (auto &f : folders) {
        for (auto &it : f.item->get_folder_infos()) {
            auto &f = it.item;
            all_fi[f->get_uuid()] = f;
        }
    }
    auto &blocks = cluster.get_blocks();
    for (auto &pair : container) {
        auto key = pair.first;
        auto &db = pair.second;
        auto folder_info_uuid = key.subspan(1, uuid_length);
        auto folder_info = all_fi[folder_info_uuid];
        assert(folder_info);

        auto option = file_info_t::create(key, db, folder_info);
        if (!option) {
            return option.assume_error();
        }
        auto &fi = option.assume_value();

        auto blocks_count = db::get_blocks_size(db);
        for (size_t i = 0; i < blocks_count; ++i) {
            auto block_hash = db::get_blocks(db, i);
            auto block = blocks.by_hash(block_hash);
            assert(block);
            fi->assign_block(std::move(block), i);
        }
        folder_info->add_relaxed(fi);
    }
    return applicator_t::apply_sibling(controller, custom);
}
