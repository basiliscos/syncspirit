// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#pragma once
#include <cstddef>
#include <string>
#include "mdbx.h"

namespace syncspirit::db {

using discr_t = std::byte;

namespace prefix {
static const constexpr discr_t misc{0x01};
static const constexpr discr_t device{0x10};
static const constexpr discr_t folder{0x11};
static const constexpr discr_t folder_info{0x12};
static const constexpr discr_t file_info{0x13};
static const constexpr discr_t ignored_device{0x14};
static const constexpr discr_t ignored_folder{0x15};
static const constexpr discr_t unknown_folder{0x16};
static const constexpr discr_t block_info{0x17};
} // namespace prefix

struct value_t {
    std::string bytes;
    MDBX_val value;

    template <typename T> value_t(T &&bytes_) noexcept : bytes{std::forward<T>(bytes_)} {
        value.iov_base = bytes.data();
        value.iov_len = bytes.length();
    }

    inline operator MDBX_val *() noexcept { return &value; }
    inline operator const MDBX_val *() const noexcept { return &value; }
};

template <discr_t> struct prefixer_t;

template <> struct prefixer_t<prefix::misc> { static value_t make(std::string_view name) noexcept; };
template <> struct prefixer_t<prefix::device> { static value_t make(std::uint64_t db_key) noexcept; };
template <> struct prefixer_t<prefix::folder> { static value_t make(std::uint64_t db_key) noexcept; };
template <> struct prefixer_t<prefix::folder_info> { static value_t make(std::uint64_t db_key) noexcept; };
template <> struct prefixer_t<prefix::file_info> { static value_t make(const std::string &db_key) noexcept; };
template <> struct prefixer_t<prefix::ignored_device> { static value_t make(const std::string &db_key) noexcept; };
template <> struct prefixer_t<prefix::ignored_folder> { static value_t make(const std::string &db_key) noexcept; };
template <> struct prefixer_t<prefix::block_info> { static value_t make(std::uint64_t db_key) noexcept; };

} // namespace syncspirit::db
