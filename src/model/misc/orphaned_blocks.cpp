// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#include "orphaned_blocks.h"
#include "model/file_info.h"

using namespace syncspirit::model;

void orphaned_blocks_t::record(file_info_t &file) { file_for_removal.emplace(&file); }

auto orphaned_blocks_t::deduce() const -> set_t {
    set_t processed;
    set_t r;
    for (auto &file : file_for_removal) {
        auto &blocks = file->get_blocks();
        for (auto &b : blocks) {
            auto key = b->get_key();
            if (processed.contains(b->get_key())) {
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
                r.emplace(std::string{key});
            }
            processed.emplace(std::string{key});
        }
    }
    return r;
}
