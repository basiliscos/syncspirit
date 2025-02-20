// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include <string_view>
#include "bytes.h"
#include <boost/outcome.hpp>
#include "syncspirit-export.h"

namespace syncspirit::utils {

namespace outcome = boost::outcome_v2;

struct SYNCSPIRIT_API base32 {

    static const char in_alphabet[];
    static const std::int32_t out_alphabet[];

    static inline std::size_t encoded_size(std::size_t dec_len) noexcept { return (dec_len + 4) / 5 * 8; }
    static inline std::size_t decoded_size(std::size_t dec_len) noexcept { return dec_len * 5 / 8; }

    static std::string encode(bytes_view_t input) noexcept;
    static outcome::result<bytes_t> decode(std::string_view input) noexcept;
};

} // namespace syncspirit::utils
