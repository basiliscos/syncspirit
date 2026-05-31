// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#pragma once

#include "hasher/messages.h"
#include "hash_base.h"

namespace syncspirit::net::local_keeper {

namespace r = rotor;

struct folder_context_t;
using folder_context_ptr_t = boost::intrusive_ptr<folder_context_t>;
struct folder_slave_t;
using folder_slave_ptr_t = r::intrusive_ptr_t<folder_slave_t>;
using hash_base_ptr_t = model::intrusive_ptr_t<hash_base_t>;

struct hash_context_t final : hasher::payload::extendended_context_t {
    hash_context_t(folder_slave_ptr_t slave_, folder_context_t *folder_context_, hash_base_ptr_t hash_file_)
        : slave{std::move(slave_)}, folder_context{folder_context_}, hash_file{std::move(hash_file_)} {}

    folder_slave_ptr_t slave;
    hash_base_ptr_t hash_file;
    folder_context_ptr_t folder_context;
};

using hash_context_ptr_t = r::intrusive_ptr_t<hash_context_t>;

} // namespace syncspirit::net::local_keeper
