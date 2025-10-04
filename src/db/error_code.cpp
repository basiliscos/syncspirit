// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "error_code.h"
#include "mdbx.h"

namespace syncspirit::db {

const static detail::db_code_category category;
const static detail::mdbx_code_category mdbx_category;

const detail::db_code_category &db_code_category() { return category; }
const detail::mdbx_code_category &mdbx_code_category() { return mdbx_category; }

namespace detail {

const char *db_code_category::name() const noexcept { return "syncspirit_db_error"; }

std::string db_code_category::message(int c) const { return mdbx_strerror(c); }

const char *mdbx_code_category::name() const noexcept { return "syncspirit_mdbx_error"; }

std::string mdbx_code_category::message(int c) const {
    std::string r;
    switch (static_cast<error_code>(c)) {
    case error_code::success:
        r = "success";
        break;
    case error_code::db_version_size_mismatch:
        r = "db version size mismatch";
        break;
    case error_code::cannot_downgrade_db:
        r = "cannot downgrade database";
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
