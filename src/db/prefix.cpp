#include "prefix.h"

using namespace syncspirit::db;

value_t prefixer_t<prefix::misc>::make(std::string_view name) noexcept {
    std::string r;
    r.resize(name.size() + 1);
    *r.data() = (char)prefix::misc;
    std::copy(name.begin(), name.end(), r.begin() + 1);
    return r;
}
