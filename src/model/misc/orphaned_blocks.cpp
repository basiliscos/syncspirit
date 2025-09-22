// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#include "orphaned_blocks.h"
#include "db/prefix.h"
#include "model/file_info.h"

using namespace syncspirit::model;

void orphaned_blocks_t::record(file_info_t &file) { file_for_removal.emplace(&file); }

auto orphaned_blocks_t::deduce(const set_t &white_listed) const -> set_t {
    auto processed = view_set_t();
    auto r = set_t();
    for (auto &file : file_for_removal) {
        if (file->is_file()) {
            auto &blocks = file->get_blocks();
            for (auto &b : blocks) {
                auto hash = b->get_hash();
                if (white_listed.contains(hash)) {
                    continue;
                }
                if (processed.contains(hash)) {
                    continue;
                }
                auto &file_blocks = b->get_file_blocks();
                auto usages = file_blocks.size();
                for (auto &fb : file_blocks) {
                    auto target_file = file_info_ptr_t(fb.file());
                    auto it = file_for_removal.find(target_file);
                    if (it != file_for_removal.end()) {
                        --usages;
                    }
                }
                if (!usages) {
                    auto copy = utils::bytes_t(hash.begin(), hash.end());
                    r.emplace(std::move(copy));
                }
                processed.emplace(hash);
            }
        }
    }
    return r;
}
