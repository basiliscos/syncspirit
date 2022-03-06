// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#pragma once
#include <string_view>
#include <numeric>

#include <boost/outcome.hpp>

namespace syncspirit::utils {

namespace outcome = boost::outcome_v2;

struct base32 {

    static const char in_alphabet[];
    static const std::int32_t out_alphabet[];

    static inline std::size_t encoded_size(std::size_t dec_len) noexcept { return (dec_len + 4) / 5 * 8; }
    static inline std::size_t decoded_size(std::size_t dec_len) noexcept { return dec_len * 5 / 8; }

    static std::string encode(std::string_view input) noexcept;
    static outcome::result<std::string> decode(std::string_view input) noexcept;
};

} // namespace syncspirit::utils
