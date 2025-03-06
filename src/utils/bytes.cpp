// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "bytes.h"
#include <algorithm>

namespace syncspirit::utils {

bytes_view_t::bytes_view_t(const bytes_t& bytes) noexcept: parent_t(bytes.begin(), bytes.end()) {
}

bool bytes_view_t::operator==(const bytes_view_t& other) const noexcept {
    return std::equal(begin(), end(), other.begin(), other.end());
}


bool bytes_t::operator==(const bytes_view_t& other) const noexcept {
    return std::equal(begin(), end(), other.begin(), other.end());
}

bool bytes_t::operator==(const bytes_t& other) const noexcept {
    return std::equal(begin(), end(), other.begin(), other.end());
}

bytes_t& bytes_t::operator=(bytes_view_t other) noexcept {
    auto self = &static_cast<parent_t&>(*this);
    *self = parent_t(other.begin(), other.end());
    return *this;
}

}
