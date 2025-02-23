// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include <span>
#include <vector>
#include <string_view>
#include "syncspirit-export.h"

namespace syncspirit::utils {

// using bytes_view_t = std::span<const unsigned char>;
// using bytes_t = std::vector<unsigned char>;

struct SYNCSPIRIT_API bytes_t;

struct SYNCSPIRIT_API bytes_view_t: std::span<const unsigned char> {
    using parent_t = std::span<const unsigned char>;
    using parent_t::parent_t;
    bytes_view_t(const bytes_t&) noexcept;

    inline bytes_view_t subspan(size_type offset, size_type count = std::dynamic_extent ) const noexcept {
        auto sz = count == std::dynamic_extent ? size() - offset : count;
        return bytes_view_t(data() + offset, sz);
    }

    bool operator==(const bytes_view_t&) const noexcept;
};

struct SYNCSPIRIT_API bytes_t: std::vector<unsigned char> {
    using parent_t =   std::vector<unsigned char>;
    using parent_t::parent_t;
    bool operator==(const bytes_view_t&) const noexcept;
    bool operator==(const bytes_t&) const noexcept;
};

}

namespace std {

// bool operator==(syncspirit::utils::bytes_view_t, syncspirit::utils::bytes_view_t) noexcept;

template <> struct hash<syncspirit::utils::bytes_view_t> {
    inline size_t operator()(syncspirit::utils::bytes_view_t bytes) const noexcept {
        auto ptr = (const char*)bytes.data();
        auto str = std::string_view(ptr, bytes.size());
        return std::hash<std::string_view>()(str);
    }
};

}
