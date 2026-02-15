// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025-2026 Ivan Baidakou

#pragma once

#include "hash_base.h"
#include "stack_context.h"
#include "fs/fs_slave.h"
#include <list>
#include <rotor/actor_base.h>

namespace syncspirit::net {

struct local_keeper_t;

}

namespace syncspirit::net::local_keeper {

namespace r = rotor;

struct folder_slave_t;
using allocator_t = std::pmr::polymorphic_allocator<char>;
using F = presentation::presence_t::features_t;

using folder_slave_ptr_t = r::intrusive_ptr_t<folder_slave_t>;

struct folder_context_t;
using folder_context_ptr_t = boost::intrusive_ptr<folder_context_t>;

struct folder_slave_t final : fs::fs_slave_t {
    using folder_contexts_t = std::list<folder_context_ptr_t>;

    folder_slave_t() noexcept = default;

    void push(folder_context_ptr_t context_) noexcept;
    void process_stack(stack_context_t &ctx) noexcept;
    void prepare_task() noexcept;

    bool post_process(stack_context_t &ctx) noexcept;
    bool post_process(hash_base_t &hash_file, folder_context_t* folder_ctx, hasher::message::digest_t &msg, stack_context_t &ctx) noexcept;

    folder_contexts_t folder_contexts;
};

} // namespace syncspirit::net::local_keeper
