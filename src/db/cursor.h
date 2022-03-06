// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#pragma once

#include "mdbx.h"
#include "string_view"
#include "error_code.h"
#include <boost/outcome.hpp>

namespace syncspirit {
namespace db {

namespace outcome = boost::outcome_v2;

struct transaction_t;

struct cursor_t {
    cursor_t() noexcept : impl{nullptr} {};
    cursor_t(cursor_t &&other) noexcept;
    ~cursor_t();

    template <typename F> outcome::result<void> iterate(std::string_view prefix, F &&f) noexcept {
        MDBX_val value;
        MDBX_val key;
        key.iov_base = (void *)(prefix.data());
        key.iov_len = prefix.size();
        auto r = mdbx_cursor_get(impl, &key, &value, MDBX_SET);
        if (r != MDBX_SUCCESS) {
            return outcome::success();
        }
        while ((r = mdbx_cursor_get(impl, &key, &value, MDBX_NEXT)) == MDBX_SUCCESS) {
            auto k = std::string_view(reinterpret_cast<const char *>(key.iov_base), key.iov_len);
            if (k.find_first_of(prefix) != 0) {
                break;
            }
            auto v = std::string_view(reinterpret_cast<const char *>(value.iov_base), value.iov_len);
            auto rr = f(k, v);
            if (!rr) {
                return rr.error();
            }
        }
        return outcome::success();
    }

  private:
    MDBX_cursor *impl = nullptr;
    cursor_t(MDBX_cursor *impl_) noexcept : impl{impl_} {}
    friend struct transaction_t;
};

} // namespace db
} // namespace syncspirit
