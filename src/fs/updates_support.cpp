// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "updates_support.h"

namespace syncspirit::fs::support {

file_update_t::file_update_t(std::string path_, std::string prev_path_, update_type_internal_t update_type_,
                             bool requires_refinement_) noexcept
    : path{std::move(path_)}, prev_path{std::move(prev_path_)}, update_type{update_type_},
      requires_refinement{requires_refinement_} {}

using C = file_update_comparator_t;

bool C::operator()(const file_update_t &lhs, const file_update_t &rhs) const noexcept { return lhs.path < rhs.path; }

bool C::operator()(const file_update_t &lhs, std::string_view rhs) const noexcept { return lhs.path < rhs; }

bool C::operator()(std::string_view lhs, const file_update_t &rhs) const noexcept { return lhs < rhs.path; }

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
