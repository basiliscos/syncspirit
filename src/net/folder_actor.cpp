#include "folder_actor.h"
#include "names.h"
#include <spdlog/spdlog.h>

using namespace syncspirit::net;

namespace {
namespace resource {
r::plugin::resource_id_t peer = 0;
}

} // namespace

folder_actor_t::folder_actor_t(config_t &config)
    : r::actor_base_t{config}, folder{config.folder}, device{config.device}, sync_state{sync_state_t::none} {}

void folder_actor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) {
        std::string id = "folder/";
        id += folder->id;
        p.set_identity(id, false);
    });
    plugin.with_casted<r::plugin::registry_plugin_t>(
        [&](auto &p) { p.discover_name(names::db, db, false).link(true); });
    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
        p.subscribe_actor(&folder_actor_t::on_start_sync);
        p.subscribe_actor(&folder_actor_t::on_stop_sync);
    });
}

void folder_actor_t::on_start() noexcept {
    r::actor_base_t::on_start();
    spdlog::trace("{}, on_start", identity);
}

void folder_actor_t::shutdown_start() noexcept {
    /*
    if (sync_state != sync_state_t::none) {
        resources->acquire(resource::peer);
    }
    */
    r::actor_base_t::shutdown_start();
}

bool folder_actor_t::on_unlink(const r::address_ptr_t &peer_addr) noexcept {
    spdlog::trace("{}, on_unlink", identity);
    auto it = peers_map.find(peer_addr);
    if (it != peers_map.end()) {
        auto &device = it->second;
        spdlog::debug("{}, on_unlink with {}", identity, device->device_id);
        peers_map.erase(it);
        if (peers_map.empty()) {
            sync_state = sync_state_t::none;
        }
        resources->release(resource::peer);
        return false;
    }
    return r::actor_base_t::on_unlink(peer_addr);
}

void folder_actor_t::on_start_sync(message::start_sync_t &message) noexcept {
    spdlog::trace("{}, on_start_sync", identity);
    sync_state = sync_state_t::syncing;
    auto plugin = get_plugin(r::plugin::link_client_plugin_t::class_identity);
    plugin->with_casted<r::plugin::link_client_plugin_t>([&](auto &p) {
        auto peer_addr = message.payload.peer;
        auto peer_device = message.payload.device;
        p.link(peer_addr, false, [this, peer_addr, peer_device](auto &ee) {
            if (ee) {
                return do_shutdown(ee);
            }
            spdlog::trace("{}, linked to {}", identity, peer_device->device_id);
            peers_map.emplace(peer_addr, peer_device);
            send<payload::start_reading_t>(peer_addr, get_address());
            resources->acquire(resource::peer);
        });
    });
}

void folder_actor_t::on_stop_sync(message::stop_sync_t &) noexcept {
    spdlog::trace("{}, on_stop_sync", identity);
    /*
    if (sync_state != sync_state_t::none) {
        resources->release(resource::peer);
    }
    */
    sync_state = sync_state_t::none;
}

void folder_actor_t::on_forward(message::forwarted_message_t &message) noexcept {}
