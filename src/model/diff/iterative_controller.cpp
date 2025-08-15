// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "iterative_controller.h"
#include "hasher/messages.h"
#include "model/diff/load/commit.h"
#include "model/diff/load/interrupt.h"
#include <rotor/actor_base.h>
#include <rotor/extended_error.h>

using namespace syncspirit::model::diff;

namespace {
namespace to {
struct make_error {};
} // namespace to
} // namespace

namespace rotor {

template <>
inline auto
actor_base_t::access<to::make_error, const std::error_code &, const extended_error_ptr_t &, const message_ptr_t &>(
    const std::error_code &ec, const extended_error_ptr_t &next, const message_ptr_t &request) noexcept {
    return make_error(ec, next, request);
}

} // namespace rotor

iterative_controller_base_t::iterative_controller_base_t(r::actor_base_t *owner_) noexcept : owner{owner_} {}

void iterative_controller_base_t::on_model_update(model::message::model_update_t &message) noexcept {
    LOG_TRACE(log, "on_model_update");
    if (interrupted) {
        delayed_updates.emplace_back(&message);
    } else {
        auto &p = message.payload;
        process(*p.diff, p.custom);
    }
}

void iterative_controller_base_t::process(model::diff::cluster_diff_t &diff, const void *custom) noexcept {
    auto apply_ctx = apply_context_t{};
    process(diff, &apply_ctx, custom);
}

void iterative_controller_base_t::on_model_interrupt(model::message::model_interrupt_t &message) noexcept {
    LOG_TRACE(log, "on_model_interrupt");
    auto copy = message.payload;
    copy.diff = {};
    process(*message.payload.diff, &copy, {});
    while (!interrupted && delayed_updates.size()) {
        LOG_TRACE(log, "applying delayed model update");
        auto &msg = delayed_updates.front();
        auto &p = msg->payload;
        process(*p.diff, p.custom);
        delayed_updates.pop_front();
    }
}

void iterative_controller_base_t::process(model::diff::cluster_diff_t &diff, apply_context_t *apply_context,
                                          const void *custom) noexcept {
    using T0 = const std::error_code &;
    using T1 = const r::extended_error_ptr_t &;
    using T2 = const r::message_ptr_t &;

    auto r = diff.apply(*cluster, *this, apply_context);
    if (!r) {
        LOG_ERROR(log, "error applying model diff: {}", r.assume_error().message());
        // auto ee = owner->access<to::make_error>( make_error(r.assume_error());
        auto ee = owner->access<to::make_error, T0, T1, T2>(r.assume_error(), {}, {});
        return owner->do_shutdown(ee);
    }

    r = visit_diff(diff, apply_context, custom);
    if (!r) {
        LOG_ERROR(log, "error visiting model: {}", r.assume_error().message());
        auto ee = owner->access<to::make_error, T0, T1, T2>(r.assume_error(), {}, {});
        return owner->do_shutdown(ee);
    }

    auto apply_ctx = reinterpret_cast<apply_context_t *>(apply_context);
    interrupted = (bool)apply_ctx->diff;
    if (interrupted) {
        auto &address = owner->get_address();
        auto message = r::make_message<model::payload::model_interrupt_t>(address, std::move(*apply_ctx));
        owner->send<hasher::payload::package_t>(bouncer, message);
    }
}

auto iterative_controller_base_t::visit_diff(model::diff::cluster_diff_t &diff, apply_context_t *,
                                             const void *) noexcept -> outcome::result<void> {
    return diff.visit(*this, nullptr);
}

auto iterative_controller_base_t::apply(const model::diff::load::interrupt_t &diff, model::cluster_t &cluster,
                                        void *custom) noexcept -> outcome::result<void> {
    auto ctx = static_cast<apply_context_t *>(custom);
    ctx->diff = diff.sibling;
    return outcome::success();
}

auto iterative_controller_base_t::apply(const model::diff::load::commit_t &diff, model::cluster_t &cluster,
                                        void *custom) noexcept -> outcome::result<void> {
    log->debug("committing db load, begin");
    owner->get_supervisor().put(diff.commit_message);

    commit_loading();
    owner->send<syncspirit::model::payload::thread_ready_t>(coordinator);

    log->debug("committing db load, end");
    return outcome::success();
}

void iterative_controller_base_t::commit_loading() noexcept {}
