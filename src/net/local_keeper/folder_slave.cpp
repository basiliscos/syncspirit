// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025-2026 Ivan Baidakou

#include "folder_slave.h"

#include "folder_context.h"

using namespace syncspirit::net;
using namespace syncspirit::net::local_keeper;

void folder_slave_t::push(folder_context_ptr_t context) noexcept { folder_contexts.emplace_front(std::move(context)); }

void folder_slave_t::push(folder_contexts_t contexts_) noexcept {
    auto generation = folder_contexts.size() ? folder_contexts.front()->get_generation() : 0;
    for (auto &ctx : contexts_) {
        if (generation) {
            ctx->adjust_generation(generation);
        }
        folder_contexts.push_back(std::move(ctx));
    }
}

void folder_slave_t::prepare_task() noexcept {
    assert(folder_contexts.size());
    auto folder_ctx = folder_contexts.front().get();
    tasks_in.emplace_back(folder_ctx->pop_task());
}

bool folder_slave_t::process_stack(stack_context_t &ctx) noexcept {
    assert(folder_contexts.size());
    ctx.slave = this;
    auto folder_ctx = folder_contexts.front().get();
    auto has_pending_io = folder_ctx->process_stack(ctx);
    if (folder_ctx->is_done() && !has_pending_io) {
        pop_context();
    }

    while (!folder_contexts.empty() && !has_pending_io) {
        auto folder_ctx = folder_contexts.front().get();
        has_pending_io = folder_ctx->process_stack(ctx);
        if (folder_ctx->is_done()) {
            pop_context();
        } else {
            break;
        }
    }
    return has_pending_io;
}

bool folder_slave_t::post_process(stack_context_t &ctx) noexcept {
    assert(folder_contexts.size());
    ctx.slave = this;
    auto folder_ctx = folder_contexts.front().get();
    auto has_pending_io = folder_ctx->post_process(ctx).process_stack(ctx);
    if (folder_ctx->is_done() && !has_pending_io) {
        pop_context();
    }

    while (!folder_contexts.empty() && !has_pending_io) {
        auto folder_ctx = folder_contexts.front().get();
        has_pending_io = folder_ctx->process_stack(ctx);
        if (folder_ctx->is_done()) {
            pop_context();
        } else {
            break;
        }
    }
    return has_pending_io;
}

bool folder_slave_t::post_process(hash_base_t &hash_file, folder_context_t *folder_ctx, hasher::message::digest_t &msg,
                                  stack_context_t &ctx) noexcept {
    ctx.slave = this;
    folder_ctx->post_process(hash_file, msg, ctx);

    auto has_pending_io = false;
    while (!folder_contexts.empty() && !has_pending_io) {
        auto folder_ctx = folder_contexts.front().get();
        has_pending_io = folder_ctx->process_stack(ctx);
        if (folder_ctx->is_done()) {
            pop_context();
        } else {
            break;
        }
    }
    return has_pending_io;
}

void folder_slave_t::pop_context() noexcept {
    auto it = folder_contexts.begin();
    if (folder_contexts.size() > 1) {
        auto next = it;
        std::advance(next, 1);
        next->get()->consume(*it->get());
    }
    folder_contexts.pop_front();
}
