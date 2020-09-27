#pragma once
#include <string>
#include <numeric>

#include <boost/outcome.hpp>

namespace syncspirit::utils {

namespace outcome = boost::outcome_v2;

struct base32 {

    static const char in_alphabet[];
    static const std::int32_t out_alphabet[];

    static inline std::size_t encoded_size(std::size_t dec_len) noexcept { return (dec_len + 4) / 5 * 8; }
    static inline std::size_t decoded_size(std::size_t dec_len) noexcept { return dec_len * 5 / 8; }

    static std::string encode(const std::string &input) noexcept;
    static outcome::result<std::string> decode(const std::string &input) noexcept;
};

} // namespace syncspirit::utils