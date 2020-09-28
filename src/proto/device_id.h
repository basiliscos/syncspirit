#pragma once

#include <string>
#include "../utils/tls.h"

namespace syncspirit::proto {

struct device_id_t {
    static const constexpr std::size_t SHA256_B32_SIZE = 52;
    static const constexpr std::size_t CHECK_DIGIT_INT = 13;
    static const constexpr std::size_t LUHN_ITERATIONS = SHA256_B32_SIZE / 13;
    static const constexpr std::size_t LUHNIZED_SIZE = 52 + LUHN_ITERATIONS;
    static const constexpr std::size_t DASH_INT = 7;
    static const constexpr std::size_t DASH_ITERATIONS = LUHNIZED_SIZE / DASH_INT;
    static const constexpr std::size_t DASHED_SIZE = LUHNIZED_SIZE + DASH_ITERATIONS - 1;

    device_id_t() noexcept {};
    device_id_t(const utils::key_pair_t &) noexcept;
    std::string_view value;
    char dashed_[DASHED_SIZE];
};

} // namespace syncspirit::proto
