#include "dialer_actor.h"
#include "names.h"
#include "spdlog/spdlog.h"

namespace syncspirit::net {

namespace {
namespace resource {
r::plugin::resource_id_t timer = 0;
r::plugin::resource_id_t request = 1;
} // namespace resource
} // namespace

dialer_actor_t::dialer_actor_t(config_t &config)
    : r::actor_base_t{config}, redial_timeout{r::pt::milliseconds{config.dialer_config.redial_timeout}},
      device{config.device}, devices{config.devices} {
    online_map.emplace(device);
}

void dialer_actor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) { p.set_identity("dialer", false); });
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
        p.discover_name(net::names::cluster, cluster, true).link(false).callback([&](auto phase, auto &ee) {
            if (!ee && phase == r::plugin::registry_plugin_t::phase_t::linking) {
                auto p = get_plugin(r::plugin::starter_plugin_t::class_identity);
                auto plugin = static_cast<r::plugin::starter_plugin_t *>(p);
                plugin->subscribe_actor(&dialer_actor_t::on_connect, cluster);
                plugin->subscribe_actor(&dialer_actor_t::on_disconnect, cluster);
            }
        });
        p.discover_name(net::names::coordinator, coordinator, false).link(false).callback([&](auto phase, auto &ee) {
            if (!ee && phase == r::plugin::registry_plugin_t::phase_t::linking) {
                auto p = get_plugin(r::plugin::starter_plugin_t::class_identity);
                auto plugin = static_cast<r::plugin::starter_plugin_t *>(p);
                plugin->subscribe_actor(&dialer_actor_t::on_announce, coordinator);
                plugin->subscribe_actor(&dialer_actor_t::on_add, coordinator);
                plugin->subscribe_actor(&dialer_actor_t::on_remove, coordinator);
                plugin->subscribe_actor(&dialer_actor_t::on_update, coordinator);
            }
        });
        p.discover_name(names::global_discovery, global_discovery, true).link(true);
    });
    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) { p.subscribe_actor(&dialer_actor_t::on_discovery); });
}

void dialer_actor_t::on_start() noexcept {
    spdlog::trace("{}, on_start", identity);
    r::actor_base_t::on_start();
}

void dialer_actor_t::shutdown_finish() noexcept {
    spdlog::trace("{}, shutdown_finish", identity);
    r::actor_base_t::shutdown_finish();
}

void dialer_actor_t::shutdown_start() noexcept {
    spdlog::trace("{}, shutdown_start", identity);
    r::actor_base_t::shutdown_start();
    for (auto &it : discovery_map) {
        send<message::discovery_cancel_t::payload_t>(global_discovery, it.second);
    }
    while (!redial_map.empty()) {
        cancel_timer(redial_map.begin()->second);
    }
}

void dialer_actor_t::on_announce(message::announce_notification_t &) noexcept {
    spdlog::trace("{}, on_announce", identity);
    for (auto it : *devices) {
        auto &d = it.second;
        if (*d != *device) {
            discover(it.second);
        }
    }
}

void dialer_actor_t::discover(const model::device_ptr_t &peer_device) noexcept {
    if (peer_device->is_dynamic()) {
        if (global_discovery) {
            assert(global_discovery);
            auto timeout = shutdown_timeout / 2;
            auto &device_id = peer_device->device_id;
            auto req_id = request<payload::discovery_request_t>(global_discovery, device_id).send(timeout);
            discovery_map.insert_or_assign(peer_device, req_id);
            resources->acquire(resource::request);
        }
    } else {
        on_ready(peer_device, peer_device->static_addresses);
    }
}

void dialer_actor_t::schedule_redial(const model::device_ptr_t &peer_device) noexcept {
    spdlog::trace("{}, scheduling redial to {}, ", identity, peer_device->device_id);
    auto redial_timer = start_timer(redial_timeout, *this, &dialer_actor_t::on_timer);
    redial_map.insert_or_assign(peer_device, redial_timer);
    resources->acquire(resource::timer);
}

void dialer_actor_t::on_ready(const model::device_ptr_t &peer_device, const utils::uri_container_t &uris) noexcept {
    spdlog::trace("{}, on_ready to dial to {}, ", identity, peer_device->device_id);
    schedule_redial(peer_device);
    send<payload::dial_ready_notification_t>(coordinator, peer_device->device_id, uris);
}

void dialer_actor_t::on_discovery(message::discovery_response_t &res) noexcept {
    resources->release(resource::request);
    if (state != r::state_t::OPERATIONAL) {
        return;
    }

    auto &ee = res.payload.ee;
    auto &peer_id = res.payload.req->payload.request_payload->device_id;
    auto peer = devices->by_id(peer_id.get_sha256());
    assert(peer);
    auto it = discovery_map.find(peer);
    assert(it != discovery_map.end());
    if (!peer->is_online()) {
        auto &req = *res.payload.req->payload.request_payload;
        if (!ee) {
            auto &contact = res.payload.res->peer;
            if (contact) {
                auto &urls = contact.value().uris;
                on_ready(peer, urls);
            } else {
                spdlog::trace("{}, on_discovery, no contact for {} has been discovered", identity, peer_id);
                schedule_redial(peer);
            }
        } else {
            spdlog::debug("{}, on_discovery, can't discover contacts for {} :: {}", identity, req.device_id,
                          ee->message());
            schedule_redial(peer);
        }
    } else {
        spdlog::trace("{}, on_discovery, peer {} is already online, noop", identity, peer_id);
    }
    discovery_map.erase(it);
}

void dialer_actor_t::on_timer(r::request_id_t request_id, bool cancelled) noexcept {
    spdlog::trace("{}, on_timer, cancelled = {}", identity, cancelled);
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

void dialer_actor_t::on_disconnect(message::disconnect_notify_t &message) noexcept {
    auto &peer_id = message.payload.peer_device_id;
    spdlog::trace("{}, on_disconnect, peer = {}", identity, peer_id);
    auto peer = devices->by_id(peer_id.get_sha256());
    assert(peer);
    discover(peer);
}

void dialer_actor_t::on_connect(message::connect_notify_t &message) noexcept {
    auto &peer_id = message.payload.peer_device_id;
    spdlog::trace("{}, on_connect, peer = {}", identity, peer_id);

    auto peer = devices->by_id(peer_id.get_sha256());
    assert(peer);

    auto it_redial = redial_map.find(peer);
    if (it_redial != redial_map.end()) {
        cancel_timer(it_redial->second);
    }

    auto it_discovery = discovery_map.find(peer);
    if (it_discovery != discovery_map.end()) {
        send<message::discovery_cancel_t::payload_t>(global_discovery, it_discovery->second);
    }
}

void dialer_actor_t::on_add(message::add_device_t &message) noexcept {
    auto &device = message.payload.device;
    discover(device);
}

void dialer_actor_t::on_remove(message::remove_device_t &message) noexcept {
    auto &peer = message.payload.device;
    auto it_discovery = discovery_map.find(peer);
    if (it_discovery != discovery_map.end()) {
        send<message::discovery_cancel_t::payload_t>(global_discovery, it_discovery->second);
    }
    auto it_redial = redial_map.find(peer);
    if (it_redial != redial_map.end()) {
        cancel_timer(it_redial->second);
    }
}

void dialer_actor_t::on_update(message::update_device_t &message) noexcept {
    spdlog::warn("[TODO] {}, on_update", identity);
}

} // namespace syncspirit::net
