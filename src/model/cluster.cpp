#include "cluster.h"
#include "misc/file_iterator.h"
#include <spdlog/spdlog.h>

using namespace syncspirit;
using namespace syncspirit::model;

cluster_t::cluster_t(device_ptr_t device_, size_t seed_) noexcept : device(device_), uuid_generator(rng_engine) {
    rng_engine.seed(seed_);
}

#if 0
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

block_infos_map_t &cluster_t::get_deleted_blocks() noexcept { return deleted_blocks; }

update_result_t cluster_t::update(const proto::ClusterConfig &config) noexcept {
    update_result_t r;
    for (int i = 0; i < config.folders_size(); ++i) {
        auto &f = config.folders(i);
        auto folder = folders.by_id(f.id());
        if (!folder) {
            r.unknown_folders.push_back(&f);
        } else {
            bool outdated = folder->update(f);
            if (outdated) {
                r.outdated_folders.insert(&f);
            }
        }
    }
    return r;
}

void cluster_t::add_folder(const folder_ptr_t &folder) noexcept {
    folder->assign_cluster(this);
    folders.put(folder);
}

file_interator_t cluster_t::iterate_files(const device_ptr_t &peer_device) noexcept {
    // find local outdated files, which present on peer device
    assert(peer_device != device);
    return file_interator_t(*this, peer_device);
}
#endif

devices_map_t &cluster_t::get_devices() noexcept {
    return devices;
}

ignored_devices_map_t &cluster_t::get_ignored_devices() noexcept {
    return ignored_devices;
}

block_infos_map_t &cluster_t::get_blocks() noexcept {
    return blocks;
}

folders_map_t &cluster_t::get_folders() noexcept {
    return folders;
}


uuid_t cluster_t::next_uuid() noexcept {
    return uuid_generator();
}
