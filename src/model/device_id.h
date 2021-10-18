#pragma once

#include <string>
#include "../utils/tls.h"
#include "misc/arc.hpp"
#include <spdlog/fmt/ostr.h>
#include <optional>

namespace syncspirit::model {

struct device_id_t : arc_base_t<device_id_t> {
    static const constexpr std::size_t SHA256_B32_SIZE = 52;
    static const constexpr std::size_t CHECK_DIGIT_INT = 13;
    static const constexpr std::size_t LUHN_ITERATIONS = SHA256_B32_SIZE / 13;
    static const constexpr std::size_t LUHNIZED_SIZE = SHA256_B32_SIZE + LUHN_ITERATIONS;
    static const constexpr std::size_t DASH_INT = 7;
    static const constexpr std::size_t DASH_ITERATIONS = LUHNIZED_SIZE / DASH_INT;
    static const constexpr std::size_t DASHED_SIZE = LUHNIZED_SIZE + DASH_ITERATIONS - 1;

    static const constexpr size_t digest_length = 32;
    static const constexpr size_t data_length = digest_length + 1;

    static std::optional<device_id_t> from_string(std::string_view value) noexcept;
    static std::optional<device_id_t> from_sha256(std::string_view sha_256) noexcept;
    static std::optional<device_id_t> from_cert(const utils::cert_data_t &cert) noexcept;
    static std::optional<device_id_t> from_uuid(std::string_view value) noexcept;

    device_id_t() noexcept {};

    bool operator==(const device_id_t &other) const noexcept { return get_sha256() == other.get_sha256(); }
    bool operator!=(const device_id_t &other) const noexcept { return !(*this == other); }
    operator bool() const noexcept { return !value.empty(); }

    const std::string &get_value() const noexcept { return value; }
    const std::string& serialize() const noexcept { return value; }

    std::string_view get_short() const noexcept;
    std::string_view get_sha256() const noexcept { return std::string_view(hash + 1, digest_length); }
    std::string_view get_key() const noexcept { return std::string_view(hash, data_length); }

    template <typename OStream> friend OStream &operator<<(OStream &os, const device_id_t &device_id) {
        return os << device_id.get_short();
    }

  private:

    device_id_t(const utils::cert_data_t &) noexcept;
    device_id_t(std::string_view value_, std::string_view sha256_) noexcept;

    std::string value;
    char hash[data_length];
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
