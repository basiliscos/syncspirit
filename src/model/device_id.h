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
    static std::optional<device_id_t> from_sha256(const std::string &sha_256) noexcept;
    static std::optional<device_id_t> from_cert(const utils::cert_data_t &cert) noexcept;

    device_id_t() noexcept {};

    bool operator==(const device_id_t &other) const noexcept { return other.value == value; }
    bool operator!=(const device_id_t &other) const noexcept { return !(other.value == value); }
    operator bool() const noexcept { return !value.empty(); }

    const std::string &get_value() const noexcept { return value; }
    const std::string_view get_short() const noexcept;
    const std::string &get_sha256() const noexcept { return sha256; }

    template <typename OStream> friend OStream &operator<<(OStream &os, const device_id_t &device_id) {
        return os << device_id.get_short();
    }

  private:
    device_id_t(const utils::cert_data_t &) noexcept;
    device_id_t(const std::string_view &value_, const std::string_view &sha256_) noexcept
        : value(value_), sha256{sha256_} {};

    std::string value;
    std::string sha256;
};

extern const device_id_t local_device_id;

} // namespace syncspirit::model

namespace std {

template <> struct hash<syncspirit::model::device_id_t> {
    inline size_t operator()(const syncspirit::model::device_id_t &device_id) const noexcept {
        return std::hash<std::string>()(device_id.get_value());
    }
};

template <> struct less<syncspirit::model::device_id_t> {
    using device_id_t = syncspirit::model::device_id_t;
    inline bool operator()(const device_id_t &lhs, const device_id_t &rhs) const noexcept {
        return lhs.get_value() < rhs.get_value();
    }
};

} // namespace std
