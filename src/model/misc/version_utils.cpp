// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#include "version_utils.h"
#include <algorithm>

namespace syncspirit::model {

version_relation_t compare(const proto::Counter &lhs, const proto::Counter rhs) noexcept {
    if (lhs.id() == rhs.id()) {
        if (lhs.value() == rhs.value()) {
            return version_relation_t::identity;
        } else if (lhs.value() < rhs.value()) {
            return version_relation_t::older;
        } else {
            return version_relation_t::newer;
        }
    } else {
        return version_relation_t::conflict;
    }
}

version_relation_t compare(const proto::Vector &lhs, const proto::Vector &rhs) noexcept {
    auto lhs_sz = lhs.counters_size();
    auto rhs_sz = rhs.counters_size();
    int limit = std::min(lhs_sz, rhs_sz);
    int i = 0;
    for (i = 0; i < limit; ++i) {
        auto &lc = lhs.counters(i);
        auto &rc = rhs.counters(i);
        auto r = compare(lc, rc);
        switch (r) {
        case version_relation_t::identity:
            continue;
        case version_relation_t::conflict:
            return version_relation_t::conflict;
        default:
            if (i == (lhs_sz - 1) && (i == (rhs_sz - 1))) {
                return r;
            } else {
                return version_relation_t::conflict;
            }
        }
    }

    if (lhs_sz > rhs_sz) {
        return version_relation_t::newer;
    } else if (lhs_sz < rhs_sz) {
        return version_relation_t::older;
    }
    return version_relation_t::identity;
}

} // namespace syncspirit::model
