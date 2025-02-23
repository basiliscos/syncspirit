// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include <mdbx.h>
#include "syncspirit-export.h"
#include "utils/bytes.h"
#include <boost/outcome.hpp>
#include <span>

namespace syncspirit {
namespace db {

namespace outcome = boost::outcome_v2;

struct transaction_t;

struct SYNCSPIRIT_API cursor_t {
    using bytes_view_t = std::span<const unsigned char>;

    cursor_t() noexcept : impl{nullptr} {};
    cursor_t(cursor_t &&other) noexcept;
    ~cursor_t();

    template <typename F> outcome::result<void> iterate(unsigned char prefix, F &&f) noexcept {
        MDBX_val value;
        MDBX_val key;
        key.iov_base = (void *)(&prefix);
        key.iov_len = 1;
        auto r = mdbx_cursor_get(impl, &key, &value, MDBX_SET);
        if (r != MDBX_SUCCESS) {
            return outcome::success();
        }
        while ((r = mdbx_cursor_get(impl, &key, &value, MDBX_NEXT)) == MDBX_SUCCESS) {
            auto k = utils::bytes_view_t(reinterpret_cast<const unsigned char *>(key.iov_base), key.iov_len);
            if (k.front() == prefix) {
                break;
            }
            auto v = utils::bytes_view_t(reinterpret_cast<const unsigned char *>(value.iov_base), value.iov_len);
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
