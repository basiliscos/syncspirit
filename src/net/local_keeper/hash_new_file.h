// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include "hash_base.h"

namespace syncspirit::net::local_keeper {

struct hash_new_file_t : hash_base_t {
    using hash_base_t::hash_base_t;
};

} // namespace syncspirit::net::local_keeper
