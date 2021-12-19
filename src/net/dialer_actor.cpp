#include "dialer_actor.h"
#include "model/diff/peer/peer_state.h"
#include "names.h"

namespace syncspirit::net {

namespace {
namespace resource {
r::plugin::resource_id_t timer = 0;
} // namespace resource
} // namespace

dialer_actor_t::dialer_actor_t(config_t &config)
    : r::actor_base_t{config}, bep_config{config.bep_config}, cluster{config.cluster},
      redial_timeout{r::pt::milliseconds{config.dialer_config.redial_timeout}}
      {
    log = utils::get_logger("net.acceptor");
}

void dialer_actor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) { p.set_identity("dialer", false); });
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
        p.discover_name(net::names::coordinator, coordinator, false).link(false).callback([&](auto phase, auto &ee) {
            if (!ee && phase == r::plugin::registry_plugin_t::phase_t::linking) {
                auto p = get_plugin(r::plugin::starter_plugin_t::class_identity);
                auto plugin = static_cast<r::plugin::starter_plugin_t *>(p);
                plugin->subscribe_actor(&dialer_actor_t::on_announce, coordinator);
                plugin->subscribe_actor(&dialer_actor_t::on_model_update, coordinator);
            }
        });
        p.discover_name(names::peers, peers, true).link(true);
    });
}

void dialer_actor_t::on_start() noexcept {
    LOG_TRACE(log, "{}, on_start", identity);
    r::actor_base_t::on_start();
}

void dialer_actor_t::shutdown_finish() noexcept {
    LOG_TRACE(log, "{}, shutdown_finish", identity);
    r::actor_base_t::shutdown_finish();
}

void dialer_actor_t::shutdown_start() noexcept {
    LOG_TRACE(log, "{}, shutdown_start", identity);
    r::actor_base_t::shutdown_start();
    while (!redial_map.empty()) {
        cancel_timer(redial_map.begin()->second);
    }
}

void dialer_actor_t::on_announce(message::announce_notification_t &) noexcept {
    LOG_TRACE(log, "{}, on_announce", identity);
    auto& devices = cluster->get_devices();
    for (auto it : devices) {
        auto &d = it.item;
        if (d != cluster->get_device()) {
            discover(d);
        }
    }
}

void dialer_actor_t::discover(const model::device_ptr_t &peer_device) noexcept {
    if (peer_device->is_dynamic()) {
        auto &device_id = peer_device->device_id();
        send<payload::discovery_notification_t>(coordinator, device_id);
    } else {
        on_ready(peer_device, peer_device->get_uris());
    }
}

void dialer_actor_t::schedule_redial(const model::device_ptr_t &peer_device) noexcept {
    auto ms = redial_timeout.total_milliseconds();
    if (redial_map.count(peer_device) == 0) {
        LOG_TRACE(log, "{}, scheduling (re)dial to {} in {} ms", identity, peer_device->device_id(), ms);
        auto redial_timer = start_timer(redial_timeout, *this, &dialer_actor_t::on_timer);
        redial_map.insert_or_assign(peer_device, redial_timer);
        resources->acquire(resource::timer);
    }
}

void dialer_actor_t::on_ready(const model::device_ptr_t &peer_device, const utils::uri_container_t &uris) noexcept {
    if (!peer_device->is_online()) {
        auto& device_id = peer_device->device_id();
        LOG_TRACE(log, "{}, on_ready to dial to {}", identity, device_id);
        schedule_redial(peer_device);
    }
}

void dialer_actor_t::on_timer(r::request_id_t request_id, bool cancelled) noexcept {
    LOG_TRACE(log, "{}, on_timer, cancelled = {}", identity, cancelled);
    using value_t = typename redial_map_t::value_type;
    resources->release(resource::timer);
    auto predicate = [&](const value_t &val) -> bool { return val.second == request_id; };
    auto it = std::find_if(redial_map.begin(), redial_map.end(), predicate);
    assert(it != redial_map.end());
    if (!cancelled) {
        discover(it->first);
    }
    redial_map.erase(it);
}

void dialer_actor_t::on_model_update(net::message::model_update_t& msg) noexcept {
    LOG_TRACE(log, "{}, on_model_update", identity);
    auto& diff = *msg.payload.diff;
    auto r = diff.visit(*this);
    if (!r) {
        auto ee = make_error(r.assume_error());
        do_shutdown(ee);
    }
}


auto dialer_actor_t::operator()(const model::diff::peer::peer_state_t &state) noexcept -> outcome::result<void>{
    auto& devices = cluster->get_devices();
    auto peer = devices.by_sha256(state.peer_id);

    if (peer) {
        if (!state.online) {
            schedule_redial(peer);
        }
        else {
            auto it = redial_map.find(peer);
            if (it != redial_map.end()) {
                cancel_timer(it->second);
            }
        }
    }
    return outcome::success();
}


} // namespace syncspirit::net
