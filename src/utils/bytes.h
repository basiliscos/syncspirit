// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include <span>
#include <vector>
#include <string_view>

namespace syncspirit::utils {

using bytes_view_t = std::span<const unsigned char>;
using bytes_t = std::vector<unsigned char>;

}

namespace std {

bool operator==(syncspirit::utils::bytes_view_t, syncspirit::utils::bytes_view_t) noexcept;

template <> struct hash<syncspirit::utils::bytes_view_t> {
    inline size_t operator()(syncspirit::utils::bytes_view_t bytes) const noexcept {
        auto ptr = (const char*)bytes.data();
        auto str = std::string_view(ptr, bytes.size());
        return std::hash<std::string_view>()(str);
    }
};

}
