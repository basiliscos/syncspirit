// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2023 Ivan Baidakou

#pragma once

#include "utils/platform.h"
#include "mdbx.h"
#include <cstddef>
#include <string>

#include "syncspirit-export.h"

namespace syncspirit::db {

using discr_t = std::byte;

namespace prefix {
// clang-format off
static const constexpr discr_t misc           {0x01};
static const constexpr discr_t device         {0x10};
static const constexpr discr_t folder         {0x11};
static const constexpr discr_t folder_info    {0x12};
static const constexpr discr_t file_info      {0x13};
static const constexpr discr_t ignored_device {0x14};
static const constexpr discr_t ignored_folder {0x15};
static const constexpr discr_t unknown_folder {0x16};
static const constexpr discr_t block_info     {0x17};
static const constexpr discr_t unknown_device {0x18};
// clang-format on
} // namespace prefix

struct SYNCSPIRIT_API value_t {
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

template <> struct prefixer_t<prefix::misc> {
    static SYNCSPIRIT_API value_t make(std::string_view name) noexcept;
};

} // namespace syncspirit::db
