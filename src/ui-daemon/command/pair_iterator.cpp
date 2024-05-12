// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#include "pair_iterator.h"

using namespace syncspirit::daemon::command;

std::optional<pair_iterator_t::pair_t> pair_iterator_t::next(bool skip_colon) noexcept {
    auto colon = in.size();
    if (!skip_colon) {
        auto colon_pos = in.find(":");
        if (colon_pos != in.npos) {
            colon = colon_pos;
        }
    }

    auto it = in.substr(0, colon);
    auto eq = it.find("=");
    if (eq == it.npos) {
        return {};
    }

    auto f = in.substr(0, eq);
    auto s = in.substr(eq + 1, colon - (eq + 1));
    if (colon < in.size()) {
        in = in.substr(colon + 1);
    } else {
        in = "";
    }
    return pair_t{f, s};
}
