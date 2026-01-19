// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#pragma once

#include "update_type.hpp"
#include <string>
#include <unordered_set>

namespace syncspirit::fs::support {

struct file_update_t {
    std::string path;
    mutable update_type_internal_t update_type;
};

struct file_update_hash_t {
    using is_transparent = void;
    size_t operator()(const file_update_t &file_update) const noexcept;
    size_t operator()(std::string_view relative_path) const noexcept;
};

struct file_update_eq_t {
    using is_transparent = void;
    bool operator()(const file_update_t &lhs, const file_update_t &rhs) const noexcept;
    bool operator()(const file_update_t &lhs, std::string_view rhs) const noexcept;
    bool operator()(std::string_view lhs, const file_update_t &rhs) const noexcept;
};

using file_updates_t = std::unordered_set<file_update_t, file_update_hash_t, file_update_eq_t>;

} // namespace syncspirit::fs::support
