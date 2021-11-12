#include "cluster_supervisor.h"
#include "controller_actor.h"
#include "names.h"
#include "../utils/error_code.h"
#include "../hasher/hasher_proxy_actor.h"
#include "model/diff/peer/peer_state.h"
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
      cluster{config.cluster}, folders{cluster->get_folders()} {
    log = utils::get_logger("net.cluster_supervisor");
}

void cluster_supervisor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    ra::supervisor_asio_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) {
        p.set_identity(names::cluster, false);
        /*
        scan_initial = p.create_address();
        scan_new = p.create_address();
        */
    });
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
        p.register_name(names::cluster, get_address());
        /*
        p.discover_name(names::scan_actor, scan_addr, true).link(true);
        */
        p.discover_name(names::coordinator, coordinator, false).link(false).callback([&](auto phase, auto &ee) {
            if (!ee && phase == r::plugin::registry_plugin_t::phase_t::linking) {
                auto p = get_plugin(r::plugin::starter_plugin_t::class_identity);
                auto plugin = static_cast<r::plugin::starter_plugin_t *>(p);
                plugin->subscribe_actor(&cluster_supervisor_t::on_model_update, coordinator);
            }
        });

    });
    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
/*
        p.subscribe_actor(&cluster_supervisor_t::on_scan_complete_initial, scan_initial);
        p.subscribe_actor(&cluster_supervisor_t::on_scan_complete_new, scan_new);
        p.subscribe_actor(&cluster_supervisor_t::on_scan_error, scan_initial);
        p.subscribe_actor(&cluster_supervisor_t::on_scan_error, scan_new);
        p.subscribe_actor(&cluster_supervisor_t::on_file_update);
*/

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
}

void cluster_supervisor_t::shutdown_start() noexcept {
    log->trace("{}, shutdown_start", identity);

    /*
    for (auto &it : scan_folders_map) {
        send<fs::payload::scan_cancel_t>(scan_addr, it.second.request_id);
    }
    */
    ra::supervisor_asio_t::shutdown_start();
}

void cluster_supervisor_t::on_model_update(message::model_update_t &message) noexcept {
    LOG_TRACE(log, "{}, on_model_update", identity);
    auto& diff = *message.payload.diff;
    auto r = diff.visit(*this);
    if (!r) {
        auto ee = make_error(r.assume_error());
        do_shutdown(ee);
    }
}

auto cluster_supervisor_t::operator()(const model::diff::peer::peer_state_t &diff) noexcept -> outcome::result<void> {
    if (!cluster->is_tainted()) {
        auto peer = cluster->get_devices().by_sha256(diff.peer_id);
        LOG_TRACE(log, "{}, visiting peer_state_t, {} is online: {}", identity, peer->device_id(), (diff.online? "yes": "no"));
        if (diff.online) {
            /* auto addr = */
            create_actor<controller_actor_t>()
                            .bep_config(bep_config)
                            .timeout(init_timeout * 7 / 9)
                            .peer(peer)
                            .peer_addr(diff.peer_addr)
                            .request_timeout(pt::milliseconds(bep_config.request_timeout))
                            .cluster(cluster)
                            .finish();
            /*
            auto id = peer->device_id().get_sha256();
            device2addr_map.emplace(id, addr);
            addr2device_map.emplace(addr, id);
            */
        }
    }
    return outcome::success();
}


/*
void cluster_supervisor_t::scan(const model::folder_ptr_t &folder, rotor::address_ptr_t &via) noexcept {
    auto &path = folder->get_path();
    auto req_id = access<to::next_request_id>(to::next_request_id{});
    send<fs::payload::scan_request_t>(scan_addr, path, via, req_id);
    scan_folders_map.emplace(path, scan_info_t{folder, req_id});
    resources->acquire(resource::fs_scan);
}
*/

/*
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
        log->debug("{}, completed initial scan for {}", identity, folder->get_label());
        send<payload::cluster_ready_notify_t>(coordinator, cluster);
    }
    scan_folders_map.erase(it);
}

void cluster_supervisor_t::on_scan_complete_new(fs::message::scan_response_t &message) noexcept {
    auto it = on_scan_complete(message);
    auto &folder = it->second.folder;
    auto &devices = cluster->get_devices();
    for (auto &it : addr2device_map) {
        auto device = devices.by_sha256(it.second);
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
*/

void cluster_supervisor_t::on_child_shutdown(actor_base_t *actor) noexcept {
    log->trace("{}, on_child_shutdown", identity);
    ra::supervisor_asio_t::on_child_shutdown(actor);
    auto &reason = actor->get_shutdown_reason();
    if (state == r::state_t::OPERATIONAL) {
        log->debug("{}, on_child_shutdown, child {} termination: {}", identity, actor->get_identity(),
                   reason->message());
/*
        auto &addr = actor->get_address();
        auto it = addr2device_map.find(addr);
        auto it_r = device2addr_map.find(it->second);
        addr2device_map.erase(it);
        device2addr_map.erase(it_r);
*/
    }
}

/*
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
*/
