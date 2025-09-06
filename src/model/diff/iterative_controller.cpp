// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "iterative_controller.h"
#include "hasher/messages.h"
#include "model/diff/load/commit.h"
#include "model/diff/load/interrupt.h"
#include <rotor/actor_base.h>
#include <rotor/extended_error.h>
#include <thread>

using namespace syncspirit::model::diff;

namespace {
namespace to {
struct state {};
struct make_error {};
struct resources {};
} // namespace to
} // namespace

namespace rotor {

template <>
inline auto
actor_base_t::access<to::make_error, const std::error_code &, const extended_error_ptr_t &, const message_ptr_t &>(
    const std::error_code &ec, const extended_error_ptr_t &next, const message_ptr_t &request) noexcept {
    return make_error(ec, next, request);
}

template <> inline auto &actor_base_t::access<to::state>() noexcept { return state; }
template <> inline auto &actor_base_t::access<to::resources>() noexcept { return resources; }

} // namespace rotor

iterative_controller_base_t::iterative_controller_base_t(r::actor_base_t *owner_,
                                                         r::plugin::resource_id_t interrupt_) noexcept
    : owner{owner_}, interrupt{interrupt_} {}

void iterative_controller_base_t::on_model_update(model::message::model_update_t &message) noexcept {
    LOG_TRACE(log, "on_model_update");
    if (interrupted) {
        delayed_updates.emplace_back(&message);
    } else {
        auto &p = message.payload;
        auto context = apply_context_t{
            {&message},
            p.custom,
        };
        process(*p.diff, context);
    }
}

void iterative_controller_base_t::process(model::diff::cluster_diff_t &diff, apply_context_t &context) noexcept {
    process_impl(diff, context);
}

void iterative_controller_base_t::on_model_interrupt(model::message::model_interrupt_t &message) noexcept {
    using namespace model::payload;
    owner->access<to::resources>()->release(interrupt);
    LOG_TRACE(log, "on_model_interrupt");
    auto &p = message.payload;
    auto apply_context = apply_context_t(
        model_interrupt_t{std::move(p.original), p.total_blocks, p.total_files, p.loaded_blocks, p.loaded_files});
    process(*message.payload.diff, apply_context);
    while (!interrupted && delayed_updates.size()) {
        LOG_TRACE(log, "applying delayed model update");
        auto &msg = delayed_updates.front();
        auto &p = msg->payload;
        apply_context.message_payload = p.custom;
        process(*p.diff, apply_context);
        delayed_updates.pop_front();
    }
}

void iterative_controller_base_t::process_impl(model::diff::cluster_diff_t &diff,
                                               apply_context_t &apply_context) noexcept {
    using T0 = const std::error_code &;
    using T1 = const r::extended_error_ptr_t &;
    using T2 = const r::message_ptr_t &;

    auto target_diff = &diff;

    if (owner->access<to::state>() > r::state_t::OPERATIONAL) {
        LOG_DEBUG(log, "no longer operational, going to commit immediately");
        auto current = &diff;
        auto commit_diff = (model::diff::load::commit_t *)(nullptr);
        while (current && !commit_diff) {
            commit_diff = dynamic_cast<model::diff::load::commit_t *>(current);
            current = current->sibling.get();
        }
        if (!commit_diff) {
            LOG_ERROR(log, "commit diff not found");
        } else {
            target_diff = commit_diff;
        }
    }

    auto r = target_diff->apply(*this, &apply_context);
    if (!r) {
        LOG_ERROR(log, "error applying model diff: {}", r.assume_error().message());
        auto ee = owner->access<to::make_error, T0, T1, T2>(r.assume_error(), {}, {});
        return owner->do_shutdown(ee);
    }

    interrupted = (bool)apply_context.diff;
    if (interrupted) {
        using payload_t = model::payload::model_interrupt_t;
        auto &address = owner->get_address();
        auto message = r::make_message<payload_t>(address, std::move(apply_context));
        owner->send<hasher::payload::package_t>(bouncer, message);
        owner->access<to::resources>()->acquire(interrupt);
    }
}

auto iterative_controller_base_t::apply(const model::diff::load::interrupt_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    auto ctx = static_cast<apply_context_t *>(custom);
    ctx->diff = diff.sibling.get();
    return outcome::success();
}

auto iterative_controller_base_t::apply(const model::diff::load::commit_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    log->debug("committing db load, begin");
    owner->get_supervisor().put(diff.commit_message);

    commit_loading();
    owner->send<syncspirit::model::payload::thread_ready_t>(coordinator, cluster, std::this_thread::get_id());
    log->debug("committing db load, end");
    return outcome::success();
}

void iterative_controller_base_t::commit_loading() noexcept {}
