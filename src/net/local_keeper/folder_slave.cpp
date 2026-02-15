// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025-2026 Ivan Baidakou

#include "folder_slave.h"

#include "folder_context.h"

using namespace syncspirit::net;
using namespace syncspirit::net::local_keeper;

void folder_slave_t::push(folder_context_ptr_t context) noexcept { folder_contexts.emplace_front(std::move(context)); }

void folder_slave_t::prepare_task() noexcept {
    assert(folder_contexts.size());
    auto folder_ctx = folder_contexts.front().get();
    tasks_in.emplace_back(folder_ctx->pop_task());
}

void folder_slave_t::process_stack(stack_context_t &ctx) noexcept {
    ctx.slave = this;
    auto folder_ctx = folder_contexts.front().get();
    folder_ctx->process_stack(ctx);
}

bool folder_slave_t::post_process(stack_context_t &ctx) noexcept {
    assert(folder_contexts.size());
    ctx.slave = this;
    auto folder_ctx = folder_contexts.front().get();
    auto has_pending_io = folder_ctx->post_process(ctx);
    if (folder_ctx->stack.empty()) {
        folder_contexts.pop_front();
    }
    return has_pending_io;
}

bool folder_slave_t::post_process(hash_base_t &hash_file, folder_context_t *folder_ctx, hasher::message::digest_t &msg,
                                  stack_context_t &ctx) noexcept {
    assert(folder_contexts.size());
    ctx.slave = this;
    auto has_pending_io = folder_ctx->post_process(hash_file, msg, ctx);
    if (folder_contexts.front().get() == folder_ctx) {
        if (folder_ctx->stack.empty()) {
           folder_contexts.pop_front();
       }
        return has_pending_io;
    } else {
        return false;
    }
}
