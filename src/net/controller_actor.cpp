#include "controller_actor.h"
#include "names.h"
#include "spdlog/spdlog.h"
#include "../ui/messages.hpp"

using namespace syncspirit::net;

controller_actor_t::controller_actor_t(config_t &config) : r::actor_base_t{config}, device_id(config.device_id) {}

void controller_actor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
        spdlog::trace("controller_actor_t (addr = {})", (void *)address.get());
        p.register_name(names::controller, get_address());
        p.discover_name(names::coordinator, coordinator, false).link();
        p.discover_name(names::peers, peers, true).link();
    });
    plugin.with_casted<r::plugin::starter_plugin_t>(
        [&](auto &p) { p.subscribe_actor(&controller_actor_t::on_discovery_notify); });
}

void controller_actor_t::on_start() noexcept {
    r::actor_base_t::on_start();
    spdlog::trace("controller_actor_t::on_start (addr = {})", (void *)address.get());

    // temporally hard-code
    /*
    send to peers supervisor connect messages
    peer_list_t peers;
    auto sample_peer =
        model::device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD");
    peers.push_back(sample_peer.value());
    */
}

void controller_actor_t::on_discovery_notify(message::discovery_notify_t &message) noexcept {
    auto &device_id = message.payload.device_id;
    auto &peer_contact = message.payload.peer;
    // TODO check, do we need that peer
    if (peer_contact.has_value()) {
        bool has_peer = false;
        if (!has_peer) {
            using original_ptr_t = ui::payload::discovery_notification_t::net_message_ptr_t;
            send<ui::payload::discovery_notification_t>(address, original_ptr_t{&message});
        }
    }
}
