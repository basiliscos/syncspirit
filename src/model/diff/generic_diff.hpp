// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#pragma once

#include "base_diff.h"

namespace syncspirit::model::diff {

namespace tag {

struct cluster {};
struct block {};
struct contact {};

} // namespace tag

template <typename Tag> struct generic_visitor_t;

template <typename Tag> struct generic_diff_t : base_diff_t {
    using visitor_t = generic_visitor_t<Tag>;

    virtual outcome::result<void> visit(visitor_t &, void *custom) const noexcept = 0;
};

} // namespace syncspirit::model::diff
