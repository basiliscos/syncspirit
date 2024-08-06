// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "dialer_actor.h"
#include "model/diff/modify/remove_peer.h"
#include "model/diff/contact/dial_request.h"
#include "model/diff/contact/peer_state.h"
#include "model/diff/contact/update_contact.h"
#include "names.h"
#include "utils/format.hpp"
#include <cassert>

namespace syncspirit::net {

using state_t = model::device_state_t;

namespace {
namespace resource {
r::plugin::resource_id_t timer = 0;
} // namespace resource
} // namespace

dialer_actor_t::dialer_actor_t(config_t &config)
    : r::actor_base_t{config}, cluster{config.cluster},
      redial_timeout{r::pt::milliseconds{config.dialer_config.redial_timeout}},
      skip_discovers{config.dialer_config.skip_discovers}, announced{false} {}

void dialer_actor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) {
        p.set_identity("net.dialer", false);
        log = utils::get_logger(identity);
    });
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
        p.discover_name(net::names::coordinator, coordinator, false).link(false).callback([&](auto phase, auto &ee) {
            if (!ee && phase == r::plugin::registry_plugin_t::phase_t::linking) {
                auto p = get_plugin(r::plugin::starter_plugin_t::class_identity);
                auto plugin = static_cast<r::plugin::starter_plugin_t *>(p);
                plugin->subscribe_actor(&dialer_actor_t::on_announce, coordinator);
                plugin->subscribe_actor(&dialer_actor_t::on_model_update, coordinator);
                plugin->subscribe_actor(&dialer_actor_t::on_contact_update, coordinator);
            }
        });
    });
}

void dialer_actor_t::on_start() noexcept {
    LOG_TRACE(log, "on_start");
    r::actor_base_t::on_start();
    auto &devices = cluster->get_devices();
    for (auto it : devices) {
        auto &d = it.item;
        if (d != cluster->get_device()) {
            schedule_redial(d);
        }
    }
}

void dialer_actor_t::shutdown_finish() noexcept {
    LOG_TRACE(log, "shutdown_finish");
    r::actor_base_t::shutdown_finish();
}

void dialer_actor_t::shutdown_start() noexcept {
    LOG_TRACE(log, "shutdown_start");
    r::actor_base_t::shutdown_start();
    while (!redial_map.empty()) {
        auto it = redial_map.begin();
        auto &info = it->second;
        if (info.timer_id) {
            cancel_timer(*info.timer_id);
        } else {
            redial_map.erase(it);
        }
    }
}

void dialer_actor_t::on_announce(message::announce_notification_t &) noexcept {
    LOG_TRACE(log, "on_announce");
    announced = true;
    auto &devices = cluster->get_devices();
    for (auto it : devices) {
        auto &d = it.item;
        if (d != cluster->get_device()) {
            schedule_redial(d);
        }
    }
}

void dialer_actor_t::discover_or_dial(const model::device_ptr_t &peer_device) noexcept {
    auto dynamic = peer_device->is_dynamic();
    if ((dynamic && !announced) || (state != r::state_t::OPERATIONAL)) {
        return;
    }

    auto &info = redial_map[peer_device];
    info.last_attempt = clock_t::now();
    auto &device_id = peer_device->device_id();
    bool do_dial = false;
    bool do_discover = false;
    bool do_update = false;

    if (!dynamic) {
        if (peer_device->get_uris().size()) {
            do_dial = true;
        } else {
            do_update = true;
        }
    } else {
        if (!peer_device->get_uris().size()) {
            do_discover = true;
        } else if (info.skip_discovers) {
            --info.skip_discovers;
            do_dial = true;
        } else {
            do_discover = true;
        }
    }

    if (do_discover) {
        auto diff = model::diff::contact_diff_ptr_t();
        diff = new model::diff::contact::peer_state_t(*cluster, device_id.get_sha256(), nullptr, state_t::discovering);
        send<model::payload::contact_update_t>(coordinator, std::move(diff));
    }
    if (do_dial) {
        auto diff = new model::diff::contact::dial_request_t(*peer_device);
        send<model::payload::contact_update_t>(coordinator, std::move(diff), this);
        LOG_TRACE(log, "discover_or_dial, dialing to {} (dynamic = {}, discover attempts left = {})", device_id,
                  dynamic, info.skip_discovers);
    }
    if (do_update) {
        // will dial indirectly
        auto &uris = peer_device->get_static_uris();
        auto diff = new model::diff::contact::update_contact_t(*cluster, device_id, uris);
        send<model::payload::contact_update_t>(coordinator, std::move(diff), this);
    }
}

void dialer_actor_t::schedule_redial(const model::device_ptr_t &peer_device) noexcept {
    using ms_t = std::chrono::milliseconds;
    auto &info = redial_map[peer_device];
    if (info.timer_id.has_value()) {
        return;
    }

    auto delta = ms_t(redial_timeout.total_milliseconds());
    auto deadline = info.last_attempt + delta;
    auto now = clock_t::now();
    if (deadline >= now) {
        auto diff_ms = std::chrono::duration_cast<ms_t>(deadline - now).count();
        auto diff = pt::millisec(diff_ms);
        auto redial_timer = start_timer(diff, *this, &dialer_actor_t::on_timer);
        info.timer_id = redial_timer;
        LOG_TRACE(log, "scheduling (re)dial to {} in {} ms, timer_id = {}", peer_device->device_id(), diff_ms,
                  redial_timer);
        resources->acquire(resource::timer);
    } else {
        LOG_TRACE(log, "will discover '{}' immediately", peer_device->device_id());
        discover_or_dial(peer_device);
    }
}

void dialer_actor_t::on_timer(r::request_id_t request_id, bool cancelled) noexcept {
    LOG_TRACE(log, "on_timer, cancelled = {}, timer_id = {}", cancelled, request_id);
    using value_t = typename redial_map_t::value_type;
    resources->release(resource::timer);
    auto predicate = [&](const value_t &val) -> bool { return val.second.timer_id == request_id; };
    auto it = std::find_if(redial_map.begin(), redial_map.end(), predicate);
    assert(it != redial_map.end());
    auto &peer = it->first;
    auto &info = it->second;
    info.timer_id.reset();
    if (!cancelled) {
        discover_or_dial(peer);
    }
}

void dialer_actor_t::on_model_update(model::message::model_update_t &msg) noexcept {
    LOG_TRACE(log, "on_model_update");
    auto &diff = *msg.payload.diff;
    auto r = diff.visit(*this, nullptr);
    if (!r) {
        auto ee = make_error(r.assume_error());
        do_shutdown(ee);
    }
}

void dialer_actor_t::on_contact_update(model::message::contact_update_t &msg) noexcept {
    LOG_TRACE(log, "on_contact_update");
    auto &diff = *msg.payload.diff;
    auto r = diff.visit(*this, nullptr);
    if (!r) {
        auto ee = make_error(r.assume_error());
        do_shutdown(ee);
    }
}

auto dialer_actor_t::operator()(const model::diff::contact::peer_state_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    auto &devices = cluster->get_devices();
    auto peer = devices.by_sha256(diff.peer_id);

    if (peer) {
        if (diff.state == state_t::offline) {
            schedule_redial(peer);
        } else if (diff.state == state_t::online) {
            auto it = redial_map.find(peer);
            if (it != redial_map.end()) {
                auto &info = it->second;
                if (info.timer_id) {
                    cancel_timer(*info.timer_id);
                }
            }
        }
    }

    return diff.visit_next(*this, custom);
}

auto dialer_actor_t::operator()(const model::diff::contact::update_contact_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    auto &devices = cluster->get_devices();
    auto peer = devices.by_sha256(diff.device.get_sha256());
    if (!peer || peer == cluster->get_device()) {
        return diff.visit_next(*this, custom);
    }

    using state_t = model::device_state_t;
    auto state = peer->get_state();
    if (peer->get_uris().size() && (state == state_t::offline || state == state_t::discovering)) {
        LOG_TRACE(log, "dialing to {}", peer->device_id());
        auto diff = new model::diff::contact::dial_request_t(*peer);
        send<model::payload::contact_update_t>(coordinator, std::move(diff), this);
        if (state == state_t::discovering) {
            auto it = redial_map.find(peer);
            if (it != redial_map.end()) {
                it->second.skip_discovers = skip_discovers;
            }
        }
    }

    return diff.visit_next(*this, custom);
}

auto dialer_actor_t::operator()(const model::diff::modify::remove_peer_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    for (auto it = redial_map.begin(); it != redial_map.end(); ++it) {
        if (it->first->device_id().get_sha256() == diff.get_peer_sha256()) {
            auto &info = it->second;
            if (info.timer_id.has_value()) {
                cancel_timer(*info.timer_id);
            }
            redial_map.erase(it);
            break;
        }
    }
    return diff.visit_next(*this, custom);
}

} // namespace syncspirit::net
