#include "cluster_supervisor.h"
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
      folders{config.folders} {}

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
        p.subscribe_actor(&cluster_supervisor_t::on_load_folder);
        p.subscribe_actor(&cluster_supervisor_t::on_make_index);
        p.subscribe_actor(&cluster_supervisor_t::on_connect);
        load_db();
    });
}

void cluster_supervisor_t::on_start() noexcept {
    spdlog::trace("{}, on_start", identity);
    ra::supervisor_asio_t::on_start();
}

void cluster_supervisor_t::load_db() noexcept {
    resources->acquire(resource::db);
    spdlog::trace("{}, load_db, starting loading cluster...", identity);
    load_cluster(folders->begin());
}

void cluster_supervisor_t::on_load_folder(message::load_folder_response_t &message) noexcept {
    auto &folder_config = message.payload.req->payload.request_payload.folder;
    auto predicate = [&](auto &it) { return it.first == folder_config.id; };
    auto it = std::find_if(folders->begin(), folders->end(), predicate);
    assert(it != folders->end());
    auto &ec = message.payload.ec;
    if (ec) {
        spdlog::warn("{}, on_load_folder, cannot load folder {} / {} : {}", identity, folder_config.label,
                     folder_config.id, ec->message());
    } else {
        auto &folder = message.payload.res.folder;
        cluster->add_folder(folder);
    }
    load_cluster(++it);
}

void cluster_supervisor_t::on_create_folder(ui::message::create_folder_request_t &message) noexcept {
    auto &folder = message.payload.request_payload.folder;
    spdlog::trace("{}, on_create_folder, {} / {} shared with {} devices", identity, folder.label(), folder.id(),
                  folder.devices_size());
    auto timeout = init_timeout / 2;
    auto request_id = request<payload::make_index_id_request_t>(db, folder).send(timeout);
    folder_requests.emplace(request_id, &message);
}

void cluster_supervisor_t::on_make_index(message::make_index_id_response_t &message) noexcept {
    auto &request_id = message.payload.req->payload.id;
    auto it = folder_requests.find(request_id);
    auto &request = *it->second;
    auto &ec = message.payload.ec;
    if (ec) {
        reply_with_error(request, ec);
        return;
    }
    auto &payload = request.payload.request_payload;
    auto &cfg = payload.folder_config;
    sys::error_code fs_ec;
    fs::create_directories(cfg.path, fs_ec);
    if (fs_ec) {
        reply_with_error(request, ec);
        return;
    }
    auto folder = model::folder_ptr_t(new model::folder_t(cfg));
    auto &index_id = message.payload.res.index_id;
    folder->assign(payload.folder, *devices);
    folder->devices.insert(model::folder_device_t{device, index_id, model::sequence_id_t{}});
    cluster->add_folder(folder);
}

void cluster_supervisor_t::on_connect(message::connect_notify_t &message) noexcept {
    auto &payload = message.payload;
    auto &device_id = payload.peer_device_id;
    spdlog::trace("{}, on_connect, peer = ", payload.peer_device_id);
    auto &device = devices->at(device_id.get_value());
    auto unknown = cluster->update(payload.cluster_config, *devices);
    for (auto &folder : unknown) {
        send<ui::payload::new_folder_notify_t>(address, folder, device);
    }
}

void cluster_supervisor_t::load_cluster(folder_iterator_t it) noexcept {
    if (it != folders->end()) {
        auto &[folder_id, folder_config] = *it;
        auto timeout = init_timeout / 2;
        request<payload::load_folder_request_t>(db, folder_config, devices).send(timeout);
        return;
    }
    spdlog::trace("{}, load_cluster, complete", identity);
    resources->release(resource::db);
}
