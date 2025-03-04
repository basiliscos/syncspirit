// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "test_supervisor.h"
#include "model/diff/modify/clone_block.h"
#include "model/diff/modify/append_block.h"
#include "model/diff/modify/finish_file.h"
#include "model/diff/advance/remote_copy.h"
#include "net/names.h"

namespace to {
struct queue {};
struct on_timer_trigger {};
} // namespace to

template <> inline auto &rotor::supervisor_t::access<to::queue>() noexcept { return queue; }

namespace rotor {

template <>
inline auto rotor::actor_base_t::access<to::on_timer_trigger, request_id_t, bool>(request_id_t request_id,
                                                                                  bool cancelled) noexcept {
    on_timer_trigger(request_id, cancelled);
}

} // namespace rotor

using namespace syncspirit::net;
using namespace syncspirit::test;

supervisor_t::supervisor_t(config_t &cfg) : parent_t(cfg) {
    auto_finish = cfg.auto_finish;
    auto_ack_blocks = cfg.auto_ack_blocks;
    sequencer = model::make_sequencer(1234);
}

void supervisor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    parent_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) {
        p.set_identity(std::string(names::coordinator) + ".test", false);
        log = utils::get_logger(identity);
        sink = p.create_address();
    });
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
        p.register_name(names::coordinator, get_address());
        p.register_name(names::sink, get_address());
    });
    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
        p.subscribe_actor(&supervisor_t::on_model_update);
        p.subscribe_actor(&supervisor_t::on_model_sink, sink);
    });
    if (configure_callback) {
        configure_callback(plugin);
    }
}

void supervisor_t::do_start_timer(const r::pt::time_duration &, r::timer_handler_base_t &handler) noexcept {
    timers.emplace_back(&handler);
}

void supervisor_t::do_cancel_timer(r::request_id_t timer_id) noexcept {
    auto it = timers.begin();
    while (it != timers.end()) {
        auto &handler = *it;
        if (handler->request_id == timer_id) {
            auto &actor_ptr = handler->owner;
            actor_ptr->access<to::on_timer_trigger, r::request_id_t, bool>(timer_id, true);
            on_timer_trigger(timer_id, true);
            timers.erase(it);
            return;
        } else {
            ++it;
        }
    }
    assert(0 && "should not happen");
}

void supervisor_t::do_invoke_timer(r::request_id_t timer_id) noexcept {
    LOG_DEBUG(log, "{}, invoking timer {}", identity, timer_id);
    auto predicate = [&](auto &handler) { return handler->request_id == timer_id; };
    auto it = std::find_if(timers.begin(), timers.end(), predicate);
    assert(it != timers.end());
    auto &handler = *it;
    auto &actor_ptr = handler->owner;
    actor_ptr->access<to::on_timer_trigger, r::request_id_t, bool>(timer_id, false);
    timers.erase(it);
}

void supervisor_t::start() noexcept {}
void supervisor_t::shutdown() noexcept { do_shutdown(); }

void supervisor_t::enqueue(r::message_ptr_t message) noexcept {
    locality_leader->access<to::queue>().emplace_back(std::move(message));
}

void supervisor_t::on_model_update(model::message::model_update_t &msg) noexcept {
    LOG_TRACE(log, "{}, updating model", identity);
    auto &diff = msg.payload.diff;
    auto r = diff->apply(*cluster, *this);
    if (!r) {
        LOG_ERROR(log, "{}, error updating model: {}", identity, r.assume_error().message());
        do_shutdown(make_error(r.assume_error()));
    }

    r = diff->visit(*this, nullptr);
    if (!r) {
        LOG_ERROR(log, "{}, error visiting model: {}", identity, r.assume_error().message());
        do_shutdown(make_error(r.assume_error()));
    }
}

void supervisor_t::on_model_sink(model::message::model_update_t &message) noexcept {
    LOG_TRACE(log, "on_model_sink");
    auto custom = const_cast<void *>(message.payload.custom);
    auto diff_ptr = reinterpret_cast<model::diff::cluster_diff_t *>(custom);
    if (diff_ptr) {
        auto diff = model::diff::cluster_diff_ptr_t(diff_ptr, false);
        send<model::payload::model_update_t>(get_address(), std::move(diff));
    }
}

auto supervisor_t::consume_errors() noexcept -> io_errors_t { return std::move(io_errors); }

auto supervisor_t::operator()(const model::diff::local::io_failure_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    auto &errs = diff.errors;
    std::copy(errs.begin(), errs.end(), std::back_inserter(io_errors));
    return diff.visit_next(*this, custom);
}

auto supervisor_t::operator()(const model::diff::modify::finish_file_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    if (auto_finish) {
        auto folder = cluster->get_folders().by_id(diff.folder_id);
        auto file_info = folder->get_folder_infos().by_device_id(diff.peer_id);
        auto file = file_info->get_file_infos().by_name(diff.file_name);
        auto ack = model::diff::advance::advance_t::create(diff.action, *file, *sequencer);
        send<model::payload::model_update_t>(get_address(), std::move(ack), this);
    }
    return diff.visit_next(*this, custom);
}

auto supervisor_t::operator()(const model::diff::modify::append_block_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    auto ack_diff = diff.ack();
    if (auto_ack_blocks) {
        send<model::payload::model_update_t>(address, diff.ack(), this);
    } else {
        if (delayed_ack_holder) {
            delayed_ack_current = delayed_ack_current->assign_sibling(ack_diff.get());
        } else {
            delayed_ack_holder = ack_diff;
            delayed_ack_current = ack_diff.get();
        }
    }
    return diff.visit_next(*this, custom);
}

auto supervisor_t::operator()(const model::diff::modify::clone_block_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    if (auto_ack_blocks) {
        send<model::payload::model_update_t>(address, diff.ack(), this);
    }
    return diff.visit_next(*this, custom);
}
