// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#pragma once

#include <boost/system/error_code.hpp>
#include "syncspirit-export.h"

namespace syncspirit {
namespace db {

enum class error_code {
    success = 0,
    db_version_size_mismatch,
    deserialization_falure,
    invalid_device_id,
    local_device_not_found,
};

namespace detail {

class SYNCSPIRIT_API db_code_category : public boost::system::error_category {
    virtual const char *name() const noexcept override;
    virtual std::string message(int c) const override;
};

class SYNCSPIRIT_API mbdx_code_category : public boost::system::error_category {
    virtual const char *name() const noexcept override;
    virtual std::string message(int c) const override;
};

} // namespace detail

SYNCSPIRIT_API const detail::db_code_category &db_code_category();

SYNCSPIRIT_API const detail::mbdx_code_category &mbdx_code_category();

inline boost::system::error_code make_error_code(int e) { return {static_cast<int>(e), db_code_category()}; }
inline boost::system::error_code make_error_code(error_code ec) { return {static_cast<int>(ec), mbdx_code_category()}; }

} // namespace db
} // namespace syncspirit

namespace boost {
namespace system {

template <> struct is_error_code_enum<syncspirit::db::detail::db_code_category> : std::true_type {
    static const bool value = true;
};

template <> struct is_error_code_enum<syncspirit::db::detail::mbdx_code_category> : std::true_type {
    static const bool value = true;
};

} // namespace system
} // namespace boost
