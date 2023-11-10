// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2023 Ivan Baidakou

#include "prefix.h"

using namespace syncspirit::db;

static value_t mk(discr_t prefix, std::string_view name) noexcept {
    std::string r;
    r.resize(name.size() + 1);
    *r.data() = (char)prefix;
    std::copy(name.begin(), name.end(), r.begin() + 1);
    return r;
}

value_t prefixer_t<prefix::misc>::make(std::string_view name) noexcept { return mk(prefix::misc, name); }
