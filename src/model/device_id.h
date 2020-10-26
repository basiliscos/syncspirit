#pragma once

#include <string>
#include "../utils/tls.h"
#include <spdlog/fmt/ostr.h>

namespace syncspirit::model {

struct device_id_t {
    static const constexpr std::size_t SHA256_B32_SIZE = 52;
    static const constexpr std::size_t CHECK_DIGIT_INT = 13;
    static const constexpr std::size_t LUHN_ITERATIONS = SHA256_B32_SIZE / 13;
    static const constexpr std::size_t LUHNIZED_SIZE = 52 + LUHN_ITERATIONS;
    static const constexpr std::size_t DASH_INT = 7;
    static const constexpr std::size_t DASH_ITERATIONS = LUHNIZED_SIZE / DASH_INT;
    static const constexpr std::size_t DASHED_SIZE = LUHNIZED_SIZE + DASH_ITERATIONS - 1;

    device_id_t() noexcept {};
    device_id_t(const std::string &value_) noexcept : value(value_){};
    device_id_t(const utils::cert_data_t &) noexcept;

    bool operator==(const device_id_t &other) const noexcept { return other.value == value; }
    bool operator!=(const device_id_t &other) const noexcept { return !(other.value == value); }
    operator bool() const noexcept { return !value.empty(); }

    std::string value;

    template <typename OStream> friend OStream &operator<<(OStream &os, const device_id_t &device_id) {
        return os << device_id.value;
    }
};

} // namespace syncspirit::model
