// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include <algorithm>

namespace syncspirit::utils {

struct bytes_comparator_t {
    using is_transparent = void;

    template <typename T1, typename T2> bool operator()(const T1 &k1, const T2 &k2) const {
        return std::lexicographical_compare(k1.begin(), k1.end(), k2.begin(), k2.end());
    }
};

} // namespace syncspirit::utils
