#pragma once

#include <string>

namespace syncspirit::proto {

struct luhn32 {
    static char calculate(const std::string &in) noexcept;
};

} // namespace syncspirit::proto
