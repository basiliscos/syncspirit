#include "base32.h"
#include <numeric>
#include "error_code.h"

using namespace syncspirit::utils;

const char base32::in_alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
const std::int32_t base32::out_alphabet[] = {
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 0 */
    -1,                                                                                 /* 1 */
    26,                                                                                 /* 2 */
    27,                                                                                 /* 3 */
    28,                                                                                 /* 4 */
    29,                                                                                 /* 5 */
    30,                                                                                 /* 6 */
    31,                                                                                 /* 7 */
    -1,                                                                                 /* 8 */
    -1,                                                                                 /* 9 */
    -1, -1, -1, -1, -1, -1, -1,                                                         /* . */
    0,                                                                                  /* A */
    1,                                                                                  /* B */
    2,                                                                                  /* C */
    3,                                                                                  /* D */
    4,                                                                                  /* E */
    5,                                                                                  /* F */
    6,                                                                                  /* G */
    7,                                                                                  /* H */
    8,                                                                                  /* I */
    9,                                                                                  /* J */
    10,                                                                                 /* K */
    11,                                                                                 /* L */
    12,                                                                                 /* M */
    13,                                                                                 /* N */
    14,                                                                                 /* O */
    15,                                                                                 /* P */
    16,                                                                                 /* Q */
    17,                                                                                 /* R */
    18,                                                                                 /* S */
    19,                                                                                 /* T */
    20,                                                                                 /* U */
    21,                                                                                 /* V */
    22,                                                                                 /* W */
    23,                                                                                 /* X */
    24,                                                                                 /* Y */
    25,                                                                                 /* Z */
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
};

const constexpr std::uint32_t in_mask = 0b00011111;
const constexpr std::uint32_t out_mask = 0b11111111;

std::string base32::encode(std::string_view input) noexcept {
    auto encoded_sz = encoded_size(input.size());
    std::string encoded(encoded_sz, 0);
    std::uint32_t in = 0;
    std::int32_t in_bits = 0;
    std::int32_t sign_bits = 0;
    auto in_bytes = input.size();
    std::uint32_t out_index = 0;
    auto ptr = input.begin();

    /* setup and pre-allign first 2 bytes */
    auto pull_in = [&]() {
        if (in_bytes) {
            std::uint32_t in_byte = static_cast<unsigned char>(*ptr++);
            in_bits += 8;
            in = in | (in_byte << (16 - in_bits));
            --in_bytes;
        }
        sign_bits += 8;
    };
    pull_in();
    pull_in();

    while (in_bytes || in_bits > 0) {
        constexpr auto shift = 16 - 5;
        auto index = (in & (in_mask << (shift))) >> shift;
        encoded[out_index++] = in_alphabet[index];
        in_bits -= 5;
        sign_bits -= 5;
        in = (in << 5) & (std::numeric_limits<std::uint16_t>::max() - 1);
        if (sign_bits <= 8 && in_bytes) {
            pull_in();
        }
    }
    encoded.resize(out_index);
    return encoded;
}

outcome::result<std::string> base32::decode(std::string_view input) noexcept {
    auto decoded_sz = decoded_size(input.size());
    std::string decoded(decoded_sz, 0);

    std::size_t out_idx = 0;
    std::uint32_t out = 0;
    std::int32_t out_bits = 0;
    std::uint32_t in_idx = 0;
    auto ptr = input.begin();

    auto push_out = [&]() {
        if (out_bits >= 8) {
            auto shift = out_bits - 8;
            auto value = (out & (out_mask << shift)) >> shift;
            out_bits -= 8;
            decoded[out_idx++] = static_cast<char>(value);
            out = out & ((1 << shift) - 1);
        }
    };

    while (1) {
        ++in_idx;
        out = (out << 5);
        auto in_byte = *ptr++;
        std::uint32_t in_value = static_cast<std::uint32_t>(in_byte);
        auto quartet = out_alphabet[in_value];
        if (quartet == -1) {
            return error_code_t::base32_decoding_failure;
        }
        out = out | static_cast<std::uint32_t>(quartet);
        out_bits += 5;
        push_out();
        if (in_idx == input.size()) {
            break;
        }
    }

    return decoded;
}
