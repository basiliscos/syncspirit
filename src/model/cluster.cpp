#include "cluster.h"
#include <spdlog/spdlog.h>

using namespace syncspirit;
using namespace syncspirit::model;

cluster_t::cluster_t(device_ptr_t device_) noexcept : device(device_) {}

void cluster_t::assign_folders(const folders_map_t &folders_) noexcept {
    folders = folders_;
    for (auto it : folders) {
        auto &folder = it.second;
        folder->assign_device(device);
        folder->assign_cluster(this);
    }
}

void cluster_t::assign_blocks(block_infos_map_t &&blocks_) noexcept { blocks = std::move(blocks_); }

proto::ClusterConfig cluster_t::get(device_ptr_t target) noexcept {
    proto::ClusterConfig r;
    for (auto &[id, folder] : folders) {
        auto folder_opt = folder->get(target);
        if (folder_opt) {
            *(r.add_folders()) = folder_opt.value();
        }
    }
    return r;
}

folders_map_t &cluster_t::get_folders() noexcept { return folders; }

block_infos_map_t &cluster_t::get_blocks() noexcept { return blocks; }

cluster_t::unknown_folders_t cluster_t::update(const proto::ClusterConfig &config) noexcept {
    unknown_folders_t r;
    for (int i = 0; i < config.folders_size(); ++i) {
        auto &f = config.folders(i);
        auto folder = folders.by_id(f.id());
        if (!folder) {
            r.push_back(f);
        } else {
            folder->update(f);
        }
    }
    return r;
}

void cluster_t::add_folder(const folder_ptr_t &folder) noexcept { folders.put(folder); }

#if 0
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
folder_ptr_t cluster_t::folder_for_synch(const device_ptr_t &peer_device) noexcept {
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

file_info_ptr_t cluster_t::file_for_synch(const device_ptr_t &peer_device) noexcept {
    // find local outdated files, which present on peer device
    assert(peer_device != device);
    for (auto &it_f : folders) {
        auto &folder = it_f.second;
        for (auto &it_file : folder->get_file_infos()) {
            auto &file_info = it_file.second;
            if (file_info->get_size() && file_info->get_status() == file_status_t::older) {
                auto &folder_infos_map = folder->get_folder_infos();
                auto folder_info = folder_infos_map.by_id(peer_device->device_id.get_sha256());
                if (folder_info) {
                    return file_info;
                }
            }
        }
    }
    return {};
}
