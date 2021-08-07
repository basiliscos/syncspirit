#include "governor_actor.h"
#include "../net/names.h"

using namespace syncspirit::daemon;

governor_actor_t::governor_actor_t(config_t &cfg) : r::actor_base_t{cfg}, commands{std::move(cfg.commands)} {
    log = utils::get_logger("daemon.governor_actor");
}

void governor_actor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) { p.set_identity("governor", false); });
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
        p.discover_name(net::names::coordinator, coordinator, false).link(false).callback([&](auto phase, auto &ec) {
            if (!ec && phase == r::plugin::registry_plugin_t::phase_t::linking) {
                auto p = get_plugin(r::plugin::starter_plugin_t::class_identity);
                auto plugin = static_cast<r::plugin::starter_plugin_t *>(p);
                plugin->subscribe_actor(&governor_actor_t::on_cluster_ready, coordinator);
            }
        });
        p.discover_name(net::names::cluster, cluster, true).link(false);
    });
    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
        p.subscribe_actor(&governor_actor_t::on_update_peer);
        p.subscribe_actor(&governor_actor_t::on_folder_create);
    });
}

void governor_actor_t::on_start() noexcept {
    log->trace("{}, on_start", identity);
    r::actor_base_t::on_start();
}

void governor_actor_t::shutdown_start() noexcept {
    log->trace("{}, shutdown_start", identity);
    r::actor_base_t::shutdown_start();
}

void governor_actor_t::on_cluster_ready(net::message::cluster_ready_notify_t &message) noexcept {
    if (message.payload.ee) {
        return;
    }
    cluster_copy = message.payload.cluster;
    devices_copy = message.payload.devices;
    log->trace("{}, on_cluster_ready", identity);
    process();
}

void governor_actor_t::process() noexcept {
    bool again = false;
    do {
        if (commands.empty()) {
            log->debug("{}, no commands left for processing", identity);
            return;
        }
        auto &cmd = commands.front();
        again = !cmd->execute(*this);
        if (again) {
            commands.pop_front();
        }
    } while (again);
}

void governor_actor_t::cmd_add_peer(const model::device_ptr_t &peer) noexcept {
    using request_t = ui::payload::update_peer_request_t;
    request<request_t>(coordinator, peer).send(init_timeout);
}

void governor_actor_t::cmd_add_folder(const db::Folder &folder) noexcept {
    using request_t = ui::payload::create_folder_request_t;
    request<request_t>(cluster, folder).send(init_timeout);
}

void governor_actor_t::on_update_peer(ui::message::update_peer_response_t &message) noexcept {
    auto ee = message.payload.ee;
    if (ee) {
        log->error("{}, cannot update peer : {}", identity, ee->message());
        return do_shutdown(ee);
    }
    auto &peer = message.payload.req->payload.request_payload.peer;
    log->info("{}, successfully updated peer device {}", identity, peer->device_id);
    commands.pop_front();
    process();
}

void governor_actor_t::on_folder_create(ui::message::create_folder_response_t &message) noexcept {
    auto ee = message.payload.ee;
    if (ee) {
        log->error("{}, cannot create folder : {}", identity, ee->message());
        return do_shutdown(ee);
    }
    auto &folder = message.payload.req->payload.request_payload.folder;
    log->info("{}, successfully created folder {} / {}", identity, folder.label(), folder.id());
    commands.pop_front();
    process();
}
