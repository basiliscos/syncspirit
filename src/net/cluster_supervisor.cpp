#include "cluster_supervisor.h"
#include "controller_actor.h"
#include "names.h"
#include "../utils/error_code.h"
#include "../hasher/hasher_proxy_actor.h"
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
    : ra::supervisor_asio_t{config}, bep_config{config.bep_config}, hasher_threads{config.hasher_threads},
      device{config.device}, cluster{config.cluster}, devices{config.devices}, folders{cluster->get_folders()},
      ignored_folders(config.ignored_folders) {
    log = utils::get_logger("net.cluster_supervisor");
}

void cluster_supervisor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    ra::supervisor_asio_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) {
        p.set_identity(names::cluster, false);
        scan_initial = p.create_address();
        scan_new = p.create_address();
    });
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
        p.register_name(names::cluster, get_address());
        p.discover_name(names::coordinator, coordinator, false).link(false);
        p.discover_name(names::scan_actor, scan_addr, true).link(true);
        p.discover_name(names::db, db, true).link(true);
    });
    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
        p.subscribe_actor(&cluster_supervisor_t::on_create_folder);
        p.subscribe_actor(&cluster_supervisor_t::on_share_folder);
        p.subscribe_actor(&cluster_supervisor_t::on_store_new_folder);
        p.subscribe_actor(&cluster_supervisor_t::on_store_folder_info);
        p.subscribe_actor(&cluster_supervisor_t::on_update_peer);
        p.subscribe_actor(&cluster_supervisor_t::on_store_device);
        p.subscribe_actor(&cluster_supervisor_t::on_connect);
        p.subscribe_actor(&cluster_supervisor_t::on_disconnect);
        p.subscribe_actor(&cluster_supervisor_t::on_scan_complete_initial, scan_initial);
        p.subscribe_actor(&cluster_supervisor_t::on_scan_complete_new, scan_new);
        p.subscribe_actor(&cluster_supervisor_t::on_scan_error, scan_initial);
        p.subscribe_actor(&cluster_supervisor_t::on_scan_error, scan_new);
        p.subscribe_actor(&cluster_supervisor_t::on_file_update);

        create_actor<hasher::hasher_proxy_actor_t>()
            .timeout(init_timeout)
            .hasher_threads(hasher_threads)
            .name(net::names::hasher_proxy)
            .finish();
    });
}

void cluster_supervisor_t::on_start() noexcept {
    log->trace("{}, on_start, folders count = {}", identity, folders.size());
    ra::supervisor_asio_t::on_start();
    if (folders.size()) {
        for (auto &it : folders) {
            auto &folder = it.second;
            scan(folder, scan_initial);
        }
    } else {
        send<payload::cluster_ready_notify_t>(coordinator, cluster, *devices);
    }
}

void cluster_supervisor_t::shutdown_start() noexcept {
    log->trace("{}, shutdown_start", identity);

    for (auto &it : scan_folders_map) {
        send<fs::payload::scan_cancel_t>(scan_addr, it.second.request_id);
    }
    ra::supervisor_asio_t::shutdown_start();
}

void cluster_supervisor_t::scan(const model::folder_ptr_t &folder, rotor::address_ptr_t &via) noexcept {
    auto &path = folder->get_path();
    auto req_id = access<to::next_request_id>(to::next_request_id{});
    send<fs::payload::scan_request_t>(scan_addr, path, via, req_id);
    scan_folders_map.emplace(path, scan_info_t{folder, req_id});
    resources->acquire(resource::fs_scan);
}

cluster_supervisor_t::scan_foders_it
cluster_supervisor_t::on_scan_complete(fs::message::scan_response_t &message) noexcept {
    auto &file_map = *message.payload.map_info;
    auto &path = file_map.root;
    auto &ec = file_map.ec;
    log->trace("{}, on_scan_complete for {}", identity, path.c_str());
    resources->release(resource::fs_scan);
    auto it = scan_folders_map.find(path);
    assert(it != scan_folders_map.end());
    auto &folder = it->second.folder;
    if (!ec) {
        folder->update(file_map);
    } else {
        log->warn("{}, scanning {} error: {}", identity, path.c_str(), ec.message());
    }
    return it;
}

void cluster_supervisor_t::on_scan_complete_initial(fs::message::scan_response_t &message) noexcept {
    auto it = on_scan_complete(message);
    auto &folder = it->second.folder;
    if (scan_folders_map.size() == 1) {
        log->debug("{}, completed initial scan for {}", identity, folder->label());
        send<payload::cluster_ready_notify_t>(coordinator, cluster, *devices);
    }
    scan_folders_map.erase(it);
}

void cluster_supervisor_t::on_scan_complete_new(fs::message::scan_response_t &message) noexcept {
    auto it = on_scan_complete(message);
    auto &folder = it->second.folder;
    for (auto &it : addr2device_map) {
        auto device = devices->by_id(it.second);
        if (device->is_online() && folder->is_shared_with(device)) {
            send<payload::store_new_folder_notify_t>(it.first, folder);
        }
    }
}

void cluster_supervisor_t::on_scan_error(fs::message::scan_error_t &message) noexcept {
    // multiple messages might arrive
    auto &ec = message.payload.error;
    auto &path = message.payload.path;
    log->warn("{}, on_scan_error on '{}': {} ", identity, path.c_str(), ec.message());
}

void cluster_supervisor_t::on_child_shutdown(actor_base_t *actor) noexcept {
    log->trace("{}, on_child_shutdown", identity);
    ra::supervisor_asio_t::on_child_shutdown(actor);
    auto &reason = actor->get_shutdown_reason();
    if (state == r::state_t::OPERATIONAL) {
        log->debug("{}, on_child_shutdown, child {} termination: {}", identity, actor->get_identity(),
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
    log->trace("{}, on_create_folder, {} ({})", identity, folder.label(),
               (source ? source->device_id.get_short() : ""));
    request<payload::store_new_folder_request_t>(db, folder, source, source_index, cluster).send(init_timeout);
}

void cluster_supervisor_t::on_share_folder(ui::message::share_folder_request_t &message) noexcept {
    share_folder_req.reset(&message);
    auto &p = message.payload.request_payload;
    auto &folder = p.folder;
    auto &peer = p.peer;
    log->trace("{}, on_share_folder, {} with ", identity, folder->label(), peer->device_id);
    if (folder->get_folder_info(peer)) {
        auto ec = utils::make_error_code(utils::error_code_t::already_shared);
        auto reason = make_error(ec);
        reply_with_error(message, reason);
        return;
    }
    db::FolderInfo db_fi;
    auto fi = model::folder_info_ptr_t(new model::folder_info_t(db_fi, peer.get(), folder.get(), 0));
    fi->mark_dirty();
    request<payload::store_folder_info_request_t>(db, fi).send(init_timeout);
}

void cluster_supervisor_t::on_update_peer(ui::message::update_peer_request_t &message) noexcept {
    auto &device = message.payload.request_payload.peer;
    LOG_TRACE(log, "{}, on_update_peer, {}", identity, device->device_id);
    intrusive_ptr_add_ref(&message);
    request<payload::store_device_request_t>(db, device, &message).send(init_timeout);
}

void cluster_supervisor_t::on_store_device(message::store_device_response_t &message) noexcept {
    auto &payload = message.payload.req->payload.request_payload;
    auto &peer = payload.device;
    LOG_TRACE(log, "{}, on_store_device, {}", identity, peer->device_id);
    auto original_req = static_cast<ui::message::update_peer_request_t *>(payload.custom);
    auto &ee = message.payload.ee;
    if (ee) {
        reply_with_error(*original_req, ee);
    } else {
        LOG_DEBUG(log, "{}, updated, {}", identity, peer->device_id);
        auto prev_peer = devices->by_id(peer->get_id());
        bool updated = (bool)prev_peer;
        devices->put(peer);
        reply_to(*original_req);
        if (updated) {
            send<payload::update_device_t>(coordinator, prev_peer, peer);
        } else {
            send<payload::add_device_t>(coordinator, peer);
        }
    }
    intrusive_ptr_release(original_req);
}

void cluster_supervisor_t::on_store_new_folder(message::store_new_folder_response_t &message) noexcept {
    auto &f_src = message.payload.req->payload.request_payload.folder;
    log->trace("{}, on_store_new_folder, {}", identity, f_src.label());
    assert(create_folder_req);
    auto &ee = message.payload.ee;
    if (ee) {
        reply_with_error(*create_folder_req, ee);
    } else {
        auto &folder = message.payload.res.folder;
        log->debug("{}, created_folder {}/{}", identity, folder->id(), folder->label());
        reply_to(*create_folder_req, folder);
        scan(folder, scan_new);
    }
    create_folder_req.reset();
}

void cluster_supervisor_t::on_store_folder_info(message::store_folder_info_response_t &message) noexcept {
    assert(share_folder_req);
    auto &fi = message.payload.req->payload.request_payload.folder_info;
    auto device = fi->get_device();
    auto folder = fi->get_folder();
    log->trace("{}, on_store_folder_info (i.e. sharing), folder = {}, device = {}", identity, folder->label(),
               device->device_id);
    auto ee = message.payload.ee;
    if (ee) {
        log->warn("{}, cannot share folder = {} with device = {}: {}", identity, folder->label(), device->device_id,
                  ee->message());
        reply_with_error(*share_folder_req, ee);
        return;
    }
    folder->add(fi);
    reply_to(*share_folder_req);
    share_folder_req.reset();
}

void cluster_supervisor_t::on_connect(message::connect_notify_t &message) noexcept {
    auto &payload = message.payload;
    auto &device_id = payload.peer_device_id;
    log->trace("{}, on_connect, peer = {}", identity, payload.peer_device_id);
    auto peer = devices->by_id(device_id.get_sha256());
    auto &cluster_config = payload.cluster_config;
    auto addr = create_actor<controller_actor_t>()
                    .bep_config(bep_config)
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
        auto it_r = addr2device_map.find(it->second);
        addr2device_map.erase(it_r);
        device2addr_map.erase(it);
    }
}

void cluster_supervisor_t::on_file_update(message::file_update_notify_t &message) noexcept {
    auto &file = message.payload.file;
    auto folder = file->get_folder_info()->get_folder();
    for (auto &it : *devices) {
        auto &device = it.second;
        auto addr_it = device2addr_map.find(device->device_id.get_sha256());
        if (addr_it != device2addr_map.end() && folder->is_shared_with(device)) {
            send<payload::file_update_t>(addr_it->second, file);
        }
    }
}
