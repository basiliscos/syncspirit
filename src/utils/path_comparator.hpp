// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#pragma once

#include "path.h"

namespace syncspirit::utils {

struct path_comparator_t {
    using is_transparent = void;

    bool operator()(const utils::path_ptr_t& l, const utils::path_ptr_t& r) const {
        return *l < *r;
    }
    bool operator()(const utils::path_base_t& l, const utils::path_ptr_t& r) const {
        return l < *r;
    }
    bool operator()(const utils::path_ptr_t& l, const utils::path_base_t& r) const {
        return *l < r;
    }
};

}