#pragma once
#include <cstddef>
#include <string>
#include "mdbx.h"

namespace syncspirit::db {

using discr_t = std::byte;

namespace prefix {
static const constexpr discr_t misc{0x01};
static const constexpr discr_t folder_info{0x02};
static const constexpr discr_t folder_index{0x03};
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
template <> struct prefixer_t<prefix::folder_info> { static value_t make(const std::string_view &id) noexcept; };
template <> struct prefixer_t<prefix::folder_index> { static value_t make(const std::string_view &id) noexcept; };

} // namespace syncspirit::db
