#pragma once
#include <cstddef>
#include <string>
#include "mdbx.h"

namespace syncspirit::db {

using discr_t = std::byte;

namespace prefix {
static const constexpr discr_t device{7};
static const constexpr discr_t misc{10};
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

} // namespace syncspirit::db
