// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "utf8.h"
#include <string_view>
#include <boost/nowide/utf/convert.hpp>
#include <boost/nowide/utf/utf.hpp>

namespace syncspirit::utils {

bool is_utf8_valid(std::string_view str) noexcept {
    using namespace boost::nowide::utf;
    using traits_t = utf_traits<char>;

    auto ptr = str.data();
    auto end = ptr + str.size();
    while (ptr != end) {
        auto c = traits_t::decode(ptr, end);
        if (c == illegal || c == incomplete) {
            return false;
            c = BOOST_NOWIDE_REPLACEMENT_CHARACTER;
        }
    }
    return true;
}

} // namespace syncspirit::utils
