#include "luhn32.h"
#include "../utils/base32.h"
#include <cassert>
#include <string_view>
#include <boost/outcome.hpp>

using namespace syncspirit::proto;
using namespace syncspirit::utils;
namespace outcome = boost::outcome_v2;

static outcome::result<char> calc(std::string_view in) {
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

char luhn32::calculate(std::string_view in) noexcept {
    auto result = calc(in);
    assert(result && "wrong codepoint (bad input string)");
    return result.value();
}

bool luhn32::validate(std::string_view in) noexcept {
    auto length = in.length();
    if (!length) {
        return false; // not sure
    }

    auto view = std::string_view(in.data(), length - 1);
    auto result = calc(view);
    if (!result) {
        return false;
    }
    return in[length - 1] == result.value();
}
