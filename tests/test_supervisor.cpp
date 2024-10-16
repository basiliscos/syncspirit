// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2023 Ivan Baidakou

#include "test_supervisor.h"
#include "model/diff/modify/finish_file.h"
#include "model/diff/modify/finish_file_ack.h"
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
    sequencer = model::make_sequencer(1234);
}

void supervisor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    parent_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) {
        p.set_identity(std::string(names::coordinator) + ".test", false);
        log = utils::get_logger(identity);
    });
    plugin.with_casted<r::plugin::registry_plugin_t>(
        [&](auto &p) { p.register_name(names::coordinator, get_address()); });
    plugin.with_casted<r::plugin::starter_plugin_t>(
        [&](auto &p) { p.subscribe_actor(&supervisor_t::on_model_update); });
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
    auto r = diff->apply(*cluster);
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

auto supervisor_t::operator()(const model::diff::modify::finish_file_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    if (auto_finish) {
        auto folder = cluster->get_folders().by_id(diff.folder_id);
        auto file_info = folder->get_folder_infos().by_device(*cluster->get_device());
        auto file = file_info->get_file_infos().by_name(diff.file_name);
        auto ack = model::diff::cluster_diff_ptr_t{};
        ack = new model::diff::modify::finish_file_ack_t(*file, diff.peer_id);
        send<model::payload::model_update_t>(get_address(), std::move(ack), this);
    }
    return outcome::success();
}
