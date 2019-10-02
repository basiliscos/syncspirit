#include "luhn32.h"
#include "../utils/base32.h"
#include <cassert>

using namespace syncspirit::proto;
using namespace syncspirit::utils;

char luhn32::calculate(const std::string &in) noexcept {
    int factor = 1;
    int sum = 0;
    constexpr const int n = 32;

    for (auto it = in.begin(); it != in.end(); ++it) {
        std::size_t index = static_cast<unsigned char>(*it);
        int code_point = base32::out_alphabet[index];
        assert(code_point >= 0 && "wrong codepoint (bad input string)");

        int addend = factor * code_point;

        factor = (factor == 2) ? 1 : 2;
        addend = (addend / n) + (addend % n);
        sum += addend;
    }

    int remainder = sum % n;
    int check_char_index = (n - remainder) % n;

    return base32::in_alphabet[check_char_index];
}
