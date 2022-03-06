// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#include "error_code.h"
#include "mdbx.h"

namespace syncspirit::db {

const static detail::db_code_category category;
const static detail::mbdx_code_category mbdx_category;

const detail::db_code_category &db_code_category() { return category; }
const detail::mbdx_code_category &mbdx_code_category() { return mbdx_category; }

namespace detail {

const char *db_code_category::name() const noexcept { return "syncspirit_db_error"; }

std::string db_code_category::message(int c) const { return mdbx_strerror(c); }

const char *mbdx_code_category::name() const noexcept { return "syncspirit_mbdx_error"; }

std::string mbdx_code_category::message(int c) const {
    std::string r;
    switch (static_cast<error_code>(c)) {
    case error_code::success:
        r = "success";
        break;
    case error_code::db_version_size_mismatch:
        r = "db version size mismatch";
        break;
    default:
        r = "unknown";
    }
    r += " (";
    r += std::to_string(c) + ")";
    return r;
}

} // namespace detail
} // namespace syncspirit::db
