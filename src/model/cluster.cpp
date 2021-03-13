#include "cluster.h"
#include <spdlog/spdlog.h>

using namespace syncspirit;
using namespace syncspirit::model;

cluster_t::cluster_t(device_ptr_t device_) noexcept : device(device_) {}

void cluster_t::assign_folders(folders_map_t &&folders_) noexcept {
    folders = std::move(folders_);
    for (auto it : folders) {
        it.second->assign_device(device);
    }
}

proto::ClusterConfig cluster_t::get() noexcept {
    proto::ClusterConfig r;
    for (auto &[id, folder] : folders) {
        std::abort();
        //*(r.add_folders()) = folder->get();
    }
    return r;
}

const folders_map_t &cluster_t::get_folders() const noexcept { return folders; }

cluster_t::update_info_t cluster_t::update(proto::ClusterConfig &config, const devices_map_t &devices) noexcept {
    update_info_t r;
    for (int i = 0; i < config.folders_size(); ++i) {
        auto &f = config.folders(i);
        auto folder = folders.by_id(f.id());
        if (!folder) {
            r.unknown_folders.push_back(f);
        }
    }
    return r;
}

#if 0

void cluster_t::add_folder(const folder_ptr_t &folder) noexcept { folders.emplace(folder->id(), folder); }

cluster_t::folders_config_t cluster_t::serialize() noexcept {
    folders_config_t r;
    for (auto &[id, folder] : folders) {
        r.emplace(id, folder->serialize(device));
    }
    return r;
}

folder_ptr_t cluster_t::get_folder(const std::string &folder_id) noexcept {
    auto it = folders.find(folder_id);
    if (it != folders.end()) {
        return it->second;
    }
    return folder_ptr_t{};
}

cluster_t::unknown_folders_t cluster_t::update(proto::ClusterConfig &config, const devices_map_t &devices) noexcept {
    unknown_folders_t unknown;
    for (int i = 0; i < config.folders_size(); ++i) {
        auto &f = *config.mutable_folders(i);
        sanitize(f, devices);

        auto folder = get_folder(f.id());
        if (!folder) {
            unknown.emplace_back(f);
        } else {
            folder->assign(f, devices);
        }

        spdlog::debug("cluster_t::update, folder : {} / {}", f.label().c_str(), f.id().c_str());
        for (int j = 0; j < f.devices_size(); ++j) {
            auto &d = f.devices(j);
            spdlog::trace("device: name = {}, issued by {}, max sequence = {}, index_id = {}", d.name(), d.cert_name(),
                          d.max_sequence(), d.index_id());
        }
    }
    return unknown;
}

void cluster_t::sanitize(proto::Folder &folder, const devices_map_t &devices) noexcept {
    auto copy = folder;
    folder.clear_devices();
    for (int i = 0; i < copy.devices_size(); ++i) {
        auto &fd = copy.devices(i);
        auto &id = fd.id();
        auto predicate = [&id](auto &it) { return it.second->device_id.get_sha256() == id; };
        bool append = (id != device->device_id.get_sha256()) &&
                      std::find_if(devices.begin(), devices.end(), predicate) != devices.end();
        if (append) {
            *folder.add_devices() = fd;
        }
    }
}

#endif
folder_ptr_t cluster_t::opt_for_synch(const device_ptr_t &peer_device) noexcept {
    assert(peer_device != device);
    std::int64_t max_score = 0;
    folder_ptr_t best_folder;
    for (auto &it : folders) {
        auto &folder = it.second;
        auto score = folder->score(peer_device);
        if (score > max_score) {
            max_score = score;
            best_folder = folder;
        }
    }
    return best_folder;
}
