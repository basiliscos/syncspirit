#include "device_id.h"
#include <cassert>
#include "../utils/base32.h"
#include "luhn32.h"

using namespace syncspirit::proto;
using namespace syncspirit::utils;

device_id_t::device_id_t(const utils::key_pair_t &pair) noexcept {
    auto sha_result = sha256_digest(pair.cert_data);
    assert(sha_result && "cannot calc sha265 from certificate");
    auto sha = base32::encode(sha_result.value());
    assert(sha.size() == SHA256_B32_SIZE);

    auto sha_ptr = sha.data();
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

    luhn_ptr = luhnized;
    char *dashed_ptr = dashed_;
    for (std::size_t i = 0; i < DASH_ITERATIONS; ++i) {
        std::memcpy(dashed_ptr, luhn_ptr, DASH_INT);
        luhn_ptr += DASH_INT;
        dashed_ptr += DASH_INT;
        if (i < DASH_ITERATIONS - 1) {
            *dashed_ptr++ = '-';
        }
    }
    value = std::string_view(dashed_, DASHED_SIZE);
}
