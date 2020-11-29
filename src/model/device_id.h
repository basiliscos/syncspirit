#pragma once

#include <string>
#include "../utils/tls.h"
#include <spdlog/fmt/ostr.h>
#include <optional>

namespace syncspirit::model {

struct device_id_t {
    static const constexpr std::size_t SHA256_B32_SIZE = 52;
    static const constexpr std::size_t CHECK_DIGIT_INT = 13;
    static const constexpr std::size_t LUHN_ITERATIONS = SHA256_B32_SIZE / 13;
    static const constexpr std::size_t LUHNIZED_SIZE = SHA256_B32_SIZE + LUHN_ITERATIONS;
    static const constexpr std::size_t DASH_INT = 7;
    static const constexpr std::size_t DASH_ITERATIONS = LUHNIZED_SIZE / DASH_INT;
    static const constexpr std::size_t DASHED_SIZE = LUHNIZED_SIZE + DASH_ITERATIONS - 1;

    static std::optional<device_id_t> from_string(const std::string &value) noexcept;

    device_id_t() noexcept {};
    device_id_t(const utils::cert_data_t &) noexcept;

    bool operator==(const device_id_t &other) const noexcept { return other.value == value; }
    bool operator!=(const device_id_t &other) const noexcept { return !(other.value == value); }
    operator bool() const noexcept { return !value.empty(); }

    const std::string &get_value() const noexcept { return value; }
    const std::string &get_sha256() const noexcept { return sha256; }

    template <typename OStream> friend OStream &operator<<(OStream &os, const device_id_t &device_id) {
        return os << device_id.value;
    }

  private:
    device_id_t(const std::string_view &value_, const std::string_view &sha256_) noexcept
        : value(value_), sha256{sha256_} {};

    std::string value;
    std::string sha256;
};

} // namespace syncspirit::model
