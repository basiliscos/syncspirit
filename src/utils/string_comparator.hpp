// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2022 Ivan Baidakou

#pragma once

#include <cstring>
#include <string>
#include <string_view>

namespace syncspirit::utils {

struct string_comparator_t {
    using is_transparent = void;

    template <typename T1, typename T2> bool operator()(const T1 &k1, const T2 &k2) const { return k1 < k2; }
};

struct string_hash_t {
    using is_transparent = void;

    std::size_t operator()(const char *value) const { return (*this)(std::string_view(value)); }
    std::size_t operator()(std::string_view value) const { return std::hash<std::string_view>()(value); }
    std::size_t operator()(const std::string &value) const { return std::hash<std::string>()(value); }
};

struct string_eq_t {
    using is_transparent = void;

    template <typename T, typename U> auto operator()(const T &lhs, U &rhs) const { return lhs == rhs; }
};

} // namespace syncspirit::utils
