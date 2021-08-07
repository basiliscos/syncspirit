#include "cluster_supervisor.h"
#include "controller_actor.h"
#include "names.h"
#include <spdlog/spdlog.h>
#include <boost/filesystem.hpp>

namespace fs = boost::filesystem;

using namespace syncspirit::net;

namespace {
namespace resource {
r::plugin::resource_id_t fs_scan = 0;
}

namespace to {
struct next_request_id {};
} // namespace to

} // namespace

namespace rotor {

template <>
inline auto supervisor_t::access<to::next_request_id, to::next_request_id>(const to::next_request_id) noexcept {
    return next_request_id();
}
} // namespace rotor

typedef void (*callback_t)(cluster_supervisor_t *, syncspirit::model::folder_ptr_t &);

cluster_supervisor_t::cluster_supervisor_t(cluster_supervisor_config_t &config)
    : ra::supervisor_asio_t{config}, bep_config{config.bep_config}, device{config.device}, cluster{config.cluster},
      devices{config.devices}, folders{cluster->get_folders()}, ignored_folders(config.ignored_folders) {}

void cluster_supervisor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    ra::supervisor_asio_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) { p.set_identity(names::cluster, false); });
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
        p.register_name(names::cluster, get_address());
        p.discover_name(names::coordinator, coordinator, false).link(false);
        p.discover_name(names::fs, fs, true).link(true);
        p.discover_name(names::db, db, true).link(true);
    });
    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
        p.subscribe_actor(&cluster_supervisor_t::on_create_folder);
        p.subscribe_actor(&cluster_supervisor_t::on_store_new_folder);
        p.subscribe_actor(&cluster_supervisor_t::on_connect);
        p.subscribe_actor(&cluster_supervisor_t::on_disconnect);
        p.subscribe_actor(&cluster_supervisor_t::on_scan_complete);
        p.subscribe_actor(&cluster_supervisor_t::on_scan_error);
    });
}

void cluster_supervisor_t::on_start() noexcept {
    spdlog::trace("{}, on_start, folders count = {}", identity, folders.size());
    ra::supervisor_asio_t::on_start();
    if (folders.size()) {
        for (auto &it : folders) {
            auto &folder = it.second;
            callback_t hanlder = [](cluster_supervisor_t *self, model::folder_ptr_t &folder) {
                self->handle_scan_initial(folder);
            };
            scan(folder, reinterpret_cast<void *>(hanlder));
        }
    } else {
        send<payload::cluster_ready_notify_t>(coordinator, cluster, *devices);
    }
}

void cluster_supervisor_t::shutdown_start() noexcept {
    spdlog::trace("{}, shutdown_start", identity);

    for (auto &it : scan_folders_map) {
        send<fs::payload::scan_cancel_t>(fs, it.second.request_id);
    }
    ra::supervisor_asio_t::shutdown_start();
}

void cluster_supervisor_t::handle_scan_initial(model::folder_ptr_t &folder) noexcept {
    if (scan_folders_map.size() == 1) {
        spdlog::debug("{}, completed initial scan for {}", identity, folder->label());
        send<payload::cluster_ready_notify_t>(coordinator, cluster, *devices);
    }
}

void cluster_supervisor_t::handle_scan_new(model::folder_ptr_t &folder) noexcept {
    for (auto &it : addr2device_map) {
        auto device = devices->by_id(it.second);
        if (device->is_online() && folder->is_shared_with(device)) {
            send<payload::store_new_folder_notify_t>(it.first, folder);
        }
    }
}

void cluster_supervisor_t::scan(const model::folder_ptr_t &folder, void *scan_handler) noexcept {
    auto path = folder->get_path();
    auto req_id = access<to::next_request_id>(to::next_request_id{});
    send<fs::payload::scan_request_t>(fs, path, get_address(), req_id, scan_handler);
    scan_folders_map.emplace(path, scan_info_t{folder, req_id});
    resources->acquire(resource::fs_scan);
}

void cluster_supervisor_t::on_scan_complete(fs::message::scan_response_t &message) noexcept {
    auto &file_map = *message.payload.map_info;
    auto &path = file_map.root;
    auto &ec = file_map.ec;
    spdlog::trace("{}, on_scan_complete for {}", identity, path.c_str());
    resources->release(resource::fs_scan);
    auto it = scan_folders_map.find(path);
    assert(it != scan_folders_map.end());
    auto &folder = it->second.folder;
    if (!ec) {
        folder->update(file_map);
    } else {
        spdlog::warn("{}, scanning {} error: {}", identity, path.c_str(), ec.message());
    }

    callback_t handler = reinterpret_cast<callback_t>(message.payload.custom_payload);
    handler(this, folder);
    scan_folders_map.erase(it);
}

void cluster_supervisor_t::on_scan_error(fs::message::scan_error_t &message) noexcept {
    // multiple messages might arrive
    auto &ec = message.payload.error;
    auto &path = message.payload.path;
    spdlog::warn("{}, on_scan_error on '{}': {} ", identity, path.c_str(), ec.message());
}

void cluster_supervisor_t::on_child_shutdown(actor_base_t *actor) noexcept {
    spdlog::trace("{}, on_child_shutdown", identity);
    ra::supervisor_asio_t::on_child_shutdown(actor);
    auto &reason = actor->get_shutdown_reason();
    if (state == r::state_t::OPERATIONAL) {
        spdlog::debug("{}, on_child_shutdown, child {} termination: {}", identity, actor->get_identity(),
                      reason->message());
        auto &addr = actor->get_address();
        auto it = addr2device_map.find(addr);
        auto it_r = device2addr_map.find(it->second);
        addr2device_map.erase(it);
        device2addr_map.erase(it_r);
    }
}

void cluster_supervisor_t::on_create_folder(ui::message::create_folder_request_t &message) noexcept {
    create_folder_req.reset(&message);
    auto &p = message.payload.request_payload;
    auto &folder = p.folder;
    auto &source = p.source;
    auto source_index = p.source_index;
    spdlog::trace("{}, on_create_folder, {} ({})", identity, folder.label(),
                  (source ? source->device_id.get_short() : ""));
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
        folder->assign_device(device);
        folder->assign_cluster(cluster.get());

        callback_t hanlder = [](cluster_supervisor_t *self, model::folder_ptr_t &folder) {
            self->handle_scan_new(folder);
        };
        reply_to(*create_folder_req, folder);
        scan(folder, reinterpret_cast<void *>(hanlder));
    }
    create_folder_req.reset();
}

void cluster_supervisor_t::on_connect(message::connect_notify_t &message) noexcept {
    auto &payload = message.payload;
    auto &device_id = payload.peer_device_id;
    spdlog::trace("{}, on_connect, peer = {}", identity, payload.peer_device_id);
    auto peer = devices->by_id(device_id.get_sha256());
    auto &cluster_config = payload.cluster_config;
    auto addr = create_actor<controller_actor_t>()
                    .timeout(init_timeout * 7 / 9)
                    .device(device)
                    .peer(peer)
                    .peer_addr(payload.peer_addr)
                    .request_timeout(pt::milliseconds(bep_config.request_timeout))
                    .cluster(cluster)
                    .peer_cluster_config(std::move(cluster_config))
                    .ignored_folders(ignored_folders)
                    .finish()
                    ->get_address();
    auto &id = peer->device_id.get_sha256();
    device2addr_map.emplace(id, addr);
    addr2device_map.emplace(addr, id);
}

void cluster_supervisor_t::on_disconnect(message::disconnect_notify_t &message) noexcept {
    auto &device_id = message.payload.peer_device_id;
    auto it = device2addr_map.find(device_id.get_sha256());
    if (it != device2addr_map.end()) {
        device2addr_map.erase(it);
        auto it_r = addr2device_map.find(it->second);
        addr2device_map.erase(it_r);
    }
}
