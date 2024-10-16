// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "file_infos.h"
#include "model/cluster.h"
#include "model/misc/error_code.h"
#include "db/prefix.h"
#include <unordered_map>

using namespace syncspirit::model::diff::load;

auto file_infos_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    using folder_info_by_id_t = std::unordered_map<std::string_view, folder_info_ptr_t>;

    auto all_fi = folder_info_by_id_t{};
    auto &folders = cluster.get_folders();
    for (auto f : folders) {
        for (auto it : f.item->get_folder_infos()) {
            auto &f = it.item;
            all_fi[f->get_uuid()] = f;
        }
    }

    auto &blocks = cluster.get_blocks();

    for (auto &pair : container) {
        auto key = pair.key;
        auto folder_info_uuid = key.substr(1, uuid_length);
        auto folder_info = all_fi[folder_info_uuid];
        assert(folder_info);

        auto data = pair.value;
        db::FileInfo db;
        auto ok = db.ParseFromArray(data.data(), data.size());
        if (!ok) {
            return make_error_code(error_code_t::file_info_deserialization_failure);
        }

        auto option = file_info_t::create(key, db, folder_info);
        if (!option) {
            return option.assume_error();
        }
        auto &fi = option.assume_value();
        folder_info->add(fi, false);

        for (int i = 0; i < db.blocks_size(); ++i) {
            auto block_hash = db.blocks(i);
            auto block = blocks.get(block_hash);
            assert(block);
            fi->assign_block(block, (size_t)i);
        }
    }
    return applicator_t::apply_sibling(cluster);
}
