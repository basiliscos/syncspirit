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
