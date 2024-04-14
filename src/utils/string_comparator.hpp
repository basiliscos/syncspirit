// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2022 Ivan Baidakou

#pragma once

namespace syncspirit::utils {

struct string_comparator_t {
    using is_transparent = void;

    template <typename T1, typename T2> bool operator()(const T1 &k1, const T2 &k2) const { return k1 < k2; }
};

} // namespace syncspirit::utils
