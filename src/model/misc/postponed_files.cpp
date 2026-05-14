// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "postponed_files.h"

using namespace syncspirit::model;

std::size_t postponed_files_t::hasher_t::operator()(const block_2_file_ptr_t &obj) const noexcept {
    auto ptr_1 = reinterpret_cast<std::uintptr_t>(obj.block);
    auto ptr_2 = reinterpret_cast<std::uintptr_t>(obj.file);

    auto value = size_t{0};
    boost::hash_combine(value, ptr_1);
    boost::hash_combine(value, ptr_2);
    return value;
}

std::size_t postponed_files_t::hasher_t::operator()(const block_2_file_t &obj) const noexcept {
    auto ptr = block_2_file_ptr_t(obj.block.get(), obj.file.get());
    return (*this)(ptr);
}

bool postponed_files_t::eq_t::operator()(const block_2_file_t &a, const block_2_file_t &b) const noexcept {
    return a.block == b.block && a.file == b.file;
}

bool postponed_files_t::eq_t::operator()(const block_2_file_t &a, const block_2_file_ptr_t &b) const noexcept {
    return a.block == b.block && a.file == b.file;
}

bool postponed_files_t::eq_t::operator()(const block_2_file_ptr_t &a, const block_2_file_t &b) const noexcept {
    return a.block == b.block && a.file == b.file;
}

bool postponed_files_t::eq_t::operator()(const block_2_file_ptr_t &a, const block_2_file_ptr_t &b) const noexcept {
    return a.block == b.block && a.file == b.file;
}

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
    auto &ptr_proj = block_2_files.get<0>();
    auto key = block_2_file_ptr_t{block.get(), file.get()};
    if (ptr_proj.find(key) == ptr_proj.end()) {
        block_2_files.insert(block_2_file_t{block, file});
    }
}

void postponed_files_t::forget(model::file_info_t *file) noexcept {
    auto &file_proj = block_2_files.get<2>();
    for (auto it = file_proj.find(file); it != file_proj.end();) {
        it = file_proj.erase(it);
    }
    if (auto it = ready.find(file); it != ready.end()) {
        ready.erase(it);
    }
}

void postponed_files_t::advance(model::block_info_ptr_t &block) noexcept {
    auto &block_proj = block_2_files.get<1>();
    auto &files_proj = block_2_files.get<2>();

    auto b_range = block_proj.equal_range(block);
    for (auto bit = b_range.first; bit != b_range.second;) {
        auto &file = bit->file;
        auto f_range = files_proj.equal_range(file);
        if (std::distance(f_range.first, f_range.second) == 1) {
            ready.insert(bit->file);
            bit = block_proj.erase(bit);
        } else {
            ++bit;
        }
    }
}

void postponed_files_t::clear() noexcept {
    block_2_files.clear();
    ready.clear();
}
