#pragma once

#include <string_view>

namespace syncspirit::proto {

struct luhn32 {
    static char calculate(std::string_view in) noexcept;
    static bool validate(std::string_view in) noexcept;
};

} // namespace syncspirit::proto
