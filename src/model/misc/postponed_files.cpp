// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "postponed_files.h"

using namespace syncspirit::model;

auto postponed_files_t::get_ready() noexcept -> model::file_info_ptr_t {
    if (!ready.empty()) {
        auto it = ready.begin();
        auto f = std::move(*it);
        ready.erase(it);
        return f;
    }
    return {};
}

void postponed_files_t::postpone(model::block_info_ptr_t block, model::file_info_ptr_t file) noexcept {
    block_2_files.insert(block_2_file_t{block, file});
}

void postponed_files_t::forget(model::file_info_t *file) noexcept {
    auto &file_proj = block_2_files.get<1>();
    for (auto it = file_proj.find(file); it != file_proj.end();) {
        it = file_proj.erase(it);
    }
    if (auto it = ready.find(file); it != ready.end()) {
        ready.erase(it);
    }
}

void postponed_files_t::advance(model::block_info_ptr_t &block) noexcept {
    auto &block_proj = block_2_files.get<0>();
    auto &files_proj = block_2_files.get<1>();

    auto b_range = block_proj.equal_range(block);
    for (auto bit = b_range.first; bit != b_range.second; ++bit) {
        auto &file = bit->file;
        auto f_range = files_proj.equal_range(file);
        if (std::distance(f_range.first, f_range.second) == 1) {
            ready.insert(bit->file);
            block_proj.erase(bit);
            return;
        }
    }

    // worse case:
    block_proj.erase(b_range.first, b_range.second);
}

void postponed_files_t::clear() noexcept {
    block_2_files.clear();
    ready.clear();
}
