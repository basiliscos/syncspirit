// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "uri.h"

namespace syncspirit::utils {

uri_t::uri_t(boost::urls::url_view view) : parent_t(view) {
    if (!has_port()) {
        if (scheme() == "http") {
            set_port_number(80);
        } else if (scheme() == "https") {
            set_port_number(443);
        }
    }
}

uri_ptr_t uri_t::clone() const { return new uri_t(*this); }

uri_ptr_t parse(std::string_view str) {
    auto result = boost::urls::parse_uri(str);
    if (result) {
        return new uri_t(result.value());
    }
    return {};
}

} // namespace syncspirit::utils
