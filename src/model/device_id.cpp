#include "device_id.h"
#include <cassert>
#include "../utils/base32.h"
#include "../proto/luhn32.h"

using namespace syncspirit::proto;
using namespace syncspirit::model;
using namespace syncspirit::utils;

device_id_t::device_id_t(const cert_data_t &data) noexcept {
    auto sha_result = sha256_digest(data.bytes);
    assert(sha_result && "cannot calc sha256 from certificate");
    sha256 = base32::encode(sha_result.value());
    assert(sha256.size() == SHA256_B32_SIZE);

    auto sha_ptr = sha256.data();
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
    value = std::string_view(dashed, DASHED_SIZE);
}

std::optional<device_id_t> device_id_t::from_string(const std::string &value) noexcept {
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
    for (int i = 0, c = 0; i < LUHNIZED_SIZE; ++i, ++c) {
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
