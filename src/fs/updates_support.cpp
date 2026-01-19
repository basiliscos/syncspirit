// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "updates_support.h"

using namespace syncspirit::fs::support;

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
