// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "postponed_files.h"

using namespace syncspirit::model;

auto postponed_files_t::get_ready() noexcept -> files_t { return std::move(ready); }

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
    auto pre_ready = files_t();
    for (auto it = block_proj.find(block); it != block_proj.end();) {
        pre_ready.emplace(it->file);
        it = block_proj.erase(it);
    }

    auto &files_proj = block_2_files.get<1>();
    for (auto it = pre_ready.begin(); it != pre_ready.end();) {
        if (auto fit = files_proj.find(*it); fit != files_proj.end()) {
            it = pre_ready.erase(it);
        } else {
            ++it;
        }
    }

    for (auto &f : pre_ready) {
        ready.emplace(std::move(f));
    }
}

void postponed_files_t::clear() noexcept {
    block_2_files.clear();
    ready.clear();
}
