// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "updates_support.h"

namespace syncspirit::fs::support {

inline static update_type_internal_t deduce(update_type_t update_type, const update_type_internal_t *prev) {
    auto r = static_cast<update_type_internal_t>(update_type);
    if (prev) {
        if (*prev & update_type::CREATED_1) {
            r = r | update_type::CREATED_1;
        }
        if ((*prev & update_type::CONTENT) && (update_type == update_type_t::meta)) {
            r = *prev;
        }
    }
    return r;
}

file_update_t::file_update_t(std::string path_, std::string prev_path_, update_type_t update_type_,
                             const file_update_t *prev) noexcept
    : path{std::move(path_)} {
    auto initial = (prev == nullptr);
    if (initial && (update_type_ == update_type_t::created)) {
        update_type = update_type::CREATED_1;
    } else {
        update_type = deduce(update_type_, prev ? &prev->update_type : nullptr);
    }
    prev_path = (prev && !prev->prev_path.empty()) ? std::move(prev->prev_path) : std::move(prev_path_);
}

file_update_t::file_update_t(std::string path_, std::string prev_path_, update_type_internal_t update_type_) noexcept
    : path{std::move(path_)}, prev_path{std::move(prev_path_)}, update_type{update_type_} {}

void file_update_t::update(std::string prev_path_, update_type_t type_) const {
    if (prev_path.empty()) {
        prev_path = std::move(prev_path_);
    }
    update_type = deduce(type_, &this->update_type);
}

bool file_update_eq_t::operator()(const file_update_t &lhs, const file_update_t &rhs) const noexcept {
    return lhs.path == rhs.path;
}

bool file_update_eq_t::operator()(const file_update_t &lhs, std::string_view rhs) const noexcept {
    return lhs.path == rhs;
}

bool file_update_eq_t::operator()(std::string_view lhs, const file_update_t &rhs) const noexcept {
    return lhs == rhs.path;
}

size_t file_update_hash_t::operator()(const file_update_t &file_update) const noexcept {
    auto path = std::string_view(file_update.path);
    return (*this)(path);
}

size_t file_update_hash_t::operator()(std::string_view path) const noexcept {
    return std::hash<std::string_view>()(path);
}

std::string_view stringify(update_type_t type) {
    if (type == update_type_t::created) {
        return "created";
    } else if (type == update_type_t::deleted) {
        return "deleted";
    } else if (type == update_type_t::meta) {
        return "metadata changed";
    } else {
        return "content changed";
    }
}

} // namespace syncspirit::fs::support
