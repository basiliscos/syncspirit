// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#include "cursor.h"
#include <algorithm>

namespace syncspirit::db {

cursor_t::cursor_t(cursor_t &&other) noexcept { std::swap(impl, other.impl); }

cursor_t::~cursor_t() {
    if (impl) {
        mdbx_cursor_close(impl);
    }
}

} // namespace syncspirit::db
