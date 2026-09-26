// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#pragma once

#include "update_type.hpp"
#include <string>
#include <set>

namespace syncspirit::fs::support {

struct file_update_t {
    file_update_t(std::string path, std::string prev_path, update_type_internal_t update_type,
                  bool requires_refinement) noexcept;
    file_update_t() = delete;

    std::string path;
    mutable std::string prev_path;
    mutable update_type_internal_t update_type;
    mutable bool requires_refinement;
};

struct file_update_comparator_t {
    using is_transparent = void;
    bool operator()(const file_update_t &lhs, const file_update_t &rhs) const noexcept;
    bool operator()(const file_update_t &lhs, std::string_view rhs) const noexcept;
    bool operator()(std::string_view lhs, const file_update_t &rhs) const noexcept;
};

using file_updates_t = std::set<file_update_t, file_update_comparator_t>;

std::string_view stringify(update_type_t type);

} // namespace syncspirit::fs::support
