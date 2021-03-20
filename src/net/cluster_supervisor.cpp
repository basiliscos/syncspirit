#include "cluster_supervisor.h"
#include "folder_actor.h"
#include "names.h"
#include <spdlog/spdlog.h>
#include <boost/filesystem.hpp>

namespace fs = boost::filesystem;

using namespace syncspirit::net;

namespace {
namespace resource {
r::plugin::resource_id_t db = 0;
}
} // namespace

cluster_supervisor_t::cluster_supervisor_t(cluster_supervisor_config_t &config)
    : ra::supervisor_asio_t{config}, device{config.device}, cluster{config.cluster}, devices{config.devices},
      folders{cluster->get_folders()}, ignored_folders(config.ignored_folders) {}

void cluster_supervisor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    ra::supervisor_asio_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) { p.set_identity(names::cluster, false); });
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
        p.register_name(names::cluster, get_address());
        p.discover_name(names::coordinator, coordinator, false).link(false);
        p.discover_name(names::db, db, true).link(true);
    });
    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
        p.subscribe_actor(&cluster_supervisor_t::on_create_folder);
        p.subscribe_actor(&cluster_supervisor_t::on_store_new_folder);
        //        p.subscribe_actor(&cluster_supervisor_t::on_make_index);
        p.subscribe_actor(&cluster_supervisor_t::on_connect);
        p.subscribe_actor(&cluster_supervisor_t::on_disconnect);
    });
}

void cluster_supervisor_t::on_start() noexcept {
    spdlog::trace("{}, on_start", identity);

    for (auto &it : cluster->get_folders()) {
        auto &folder = it.second;
        auto addr = create_actor<folder_actor_t>()
                        .timeout(init_timeout / 2)
                        .device(device)
                        .folder(folder)
                        .finish()
                        ->get_address();
        actors_map.emplace(folder->id(), addr);
        spdlog::trace("{}, create folder actor {}, complete", identity, folder->id());
    }

    ra::supervisor_asio_t::on_start();
}

void cluster_supervisor_t::on_child_shutdown(actor_base_t *actor) noexcept {
    spdlog::trace("{}, on_start", identity);
    ra::supervisor_asio_t::on_child_shutdown(actor);
    auto &reason = actor->get_shutdown_reason();
    if (state == r::state_t::OPERATIONAL && reason->ec != r::shutdown_code_t::normal) {
        spdlog::debug("{}, on_child_shutdown, child {} abnormal termination: {}, will shut self down", identity,
                      actor->get_identity(), reason);
        auto error = r::make_error(identity, r::error_code_t::failure_escalation, reason);
        do_shutdown(error);
    }
}

void cluster_supervisor_t::on_create_folder(ui::message::create_folder_request_t &message) noexcept {
    create_folder_req.reset(&message);
    auto &p = message.payload.request_payload;
    auto &folder = p.folder;
    auto &source = p.source;
    auto source_index = p.source_index;
    spdlog::trace("{}, on_create_folder, {} (from {})", identity, folder.label(), source->device_id);
    request<payload::store_new_folder_request_t>(db, folder, source, source_index).send(init_timeout);
}

void cluster_supervisor_t::on_store_new_folder(message::store_new_folder_response_t &message) noexcept {
    auto &f_src = message.payload.req->payload.request_payload.folder;
    spdlog::trace("{}, on_store_new_folder, {}", identity, f_src.label());
    assert(create_folder_req);
    auto &ee = message.payload.ee;
    if (ee) {
        reply_with_error(*create_folder_req, ee);
    } else {
        auto &folder = message.payload.res.folder;
        spdlog::debug("{}, created_folder {}/{}", identity, folder->id(), folder->label());
        folders.put(folder);
        reply_to(*create_folder_req, std::move(folder));
    }
    create_folder_req.reset();
}

void cluster_supervisor_t::on_connect(message::connect_notify_t &message) noexcept {
    auto &payload = message.payload;
    auto &device_id = payload.peer_device_id;
    spdlog::trace("{}, on_connect, peer = {}", identity, payload.peer_device_id);
    auto device = devices->by_id(device_id.get_sha256());
    auto unknown_folders = cluster->update(payload.cluster_config);
    for (auto &folder : unknown_folders) {
        if (!ignored_folders->by_key(folder.id())) {
            send<ui::payload::new_folder_notify_t>(address, folder, device);
        }
    }
    auto folder = cluster->opt_for_synch(device);
    if (folder) {
        auto &folder_actor = actors_map.at(folder->id());
        auto &peer_addr = message.payload.peer_addr;
        send<payload::start_sync_t>(folder_actor, device, peer_addr);
        syncing_map.emplace(device_id.get_value(), folder);
    }
}

void cluster_supervisor_t::on_disconnect(message::disconnect_notify_t &message) noexcept {
    auto &device_id = message.payload.peer_device_id;
    auto it = syncing_map.find(device_id.get_value());
    if (it != syncing_map.end()) {
        auto &folder = it->second;
        auto it_folder = actors_map.find(folder->id());
        auto &folder_actor = it_folder->second;
        send<payload::stop_sync_t>(folder_actor);
        syncing_map.erase(it);
    }
}
