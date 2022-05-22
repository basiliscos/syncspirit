// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#include "device_id.h"
#include <cassert>
#include "../utils/base32.h"
#include "../proto/luhn32.h"
#include "../db/prefix.h"

#include <openssl/sha.h>

using namespace syncspirit::proto;
using namespace syncspirit::model;
using namespace syncspirit::utils;

static const constexpr char prefix = (char)(syncspirit::db::prefix::device);

device_id_t::device_id_t(std::string_view value_, std::string_view sha256_) noexcept : value{value_} {
    assert(sha256_.size() == digest_length);
    hash[0] = prefix;
    std::copy(sha256_.begin(), sha256_.end(), hash + 1);
}

device_id_t::device_id_t(device_id_t &&other) noexcept { *this = std::move(other); }

device_id_t &device_id_t::operator=(device_id_t &&other) noexcept {
    value = std::move(other.value);
    std::copy(other.hash, other.hash + data_length, hash);
    return *this;
}

device_id_t &device_id_t::operator=(const device_id_t &other) noexcept {
    value = other.value;
    std::copy(other.hash, other.hash + data_length, hash);
    return *this;
}

std::string_view device_id_t::get_short() const noexcept { return std::string_view(value.data(), DASH_INT); }

std::optional<device_id_t> device_id_t::from_string(std::string_view value) noexcept {
    using result_t = std::optional<device_id_t>;
    char buff[DASHED_SIZE];
    if (value.size() != DASHED_SIZE) {
        return result_t{};
    }
    auto buff_end = buff + DASHED_SIZE;
    std::copy(value.begin(), value.end(), buff);

    // remove dashes
    for (auto i = buff, o = buff; i != buff_end;) {
        if (*i != '-') {
            *o++ = *i++;
        } else {
            i++;
        }
    }
    buff_end = buff + LUHNIZED_SIZE;
    *buff_end = 0; /* mostly for debugging purposes */
    for (auto i = buff; i < buff_end; i += CHECK_DIGIT_INT + 1) {
        std::string_view piece(i, CHECK_DIGIT_INT + 1);
        if (!proto::luhn32::validate(piece)) {
            return result_t{};
        }
    }

    // remove luhn check digits
    auto ptr = buff;
    for (size_t i = 0, c = 0; i < LUHNIZED_SIZE; ++i, ++c) {
        if (c == CHECK_DIGIT_INT) {
            c = -1;
            // SKIP
        } else {
            *ptr++ = buff[i];
        }
    }
    buff_end = buff + LUHNIZED_SIZE - LUHN_ITERATIONS;
    *buff_end = 0; /* mostly for debugging purposes */

    auto sha = base32::decode(std::string_view(buff, LUHNIZED_SIZE - LUHN_ITERATIONS));
    if (sha) {
        return device_id_t{value, sha.value()};
    }
    return result_t{};
}

std::optional<device_id_t> device_id_t::from_sha256(std::string_view sha_256) noexcept {
    using result_t = std::optional<device_id_t>;
    auto sha256_enc = base32::encode(sha_256);
    if (sha256_enc.size() != SHA256_B32_SIZE) {
        return result_t{};
    }

    auto sha_ptr = sha256_enc.data();
    char luhnized[LUHNIZED_SIZE];
    char *luhn_ptr = luhnized;
    for (std::size_t i = 0; i < LUHN_ITERATIONS; ++i) {
        std::memcpy(luhn_ptr, sha_ptr, CHECK_DIGIT_INT);
        auto piece_ptr = luhn_ptr;
        luhn_ptr += CHECK_DIGIT_INT;
        *luhn_ptr = luhn32::calculate(std::string_view(piece_ptr, CHECK_DIGIT_INT));
        ++luhn_ptr;
        sha_ptr += CHECK_DIGIT_INT;
    }

    char dashed[DASHED_SIZE];
    luhn_ptr = luhnized;
    char *dashed_ptr = dashed;
    for (std::size_t i = 0; i < DASH_ITERATIONS; ++i) {
        std::memcpy(dashed_ptr, luhn_ptr, DASH_INT);
        luhn_ptr += DASH_INT;
        dashed_ptr += DASH_INT;
        if (i < DASH_ITERATIONS - 1) {
            *dashed_ptr++ = '-';
        }
    }
    auto str = std::string_view(dashed, DASHED_SIZE);
    return device_id_t{str, sha_256};
}

std::optional<device_id_t> device_id_t::from_cert(const cert_data_t &data) noexcept {
    using result_t = std::optional<device_id_t>;
    auto sha_result = sha256_digest(data.bytes);
    if (!sha_result) {
        return result_t{};
    }
    return from_sha256(sha_result.value());
}

namespace syncspirit::model {

static const unsigned char local_device_sha265[SHA256_DIGEST_LENGTH] = {0xFF};

const device_id_t local_device_id =
    device_id_t::from_cert(cert_data_t{std::string((char *)local_device_sha265, SHA256_DIGEST_LENGTH)}).value();

} // namespace syncspirit::model
