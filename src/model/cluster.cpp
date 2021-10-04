#include "cluster.h"
#include <spdlog/spdlog.h>

using namespace syncspirit;
using namespace syncspirit::model;

file_interator_t::file_interator_t() noexcept : cluster{nullptr} {}

file_interator_t::file_interator_t(cluster_t &cluster_, const device_ptr_t &peer_) noexcept
    : cluster{&cluster_}, peer{peer_} {
    it_folder = cluster->folders.begin();
    if (it_folder == cluster->folders.end()) {
        cluster = nullptr;
        return;
    }
    prepare();
}

void file_interator_t::prepare() noexcept {
TRY_ANEW:
    if (f_peer_it == f_peer_end) {
        for (;; ++it_folder) {
            if (it_folder == cluster->folders.end()) {
                cluster = nullptr;
                return;
            }
            auto &folder = it_folder->second;
            auto &folder_infos_map = folder->get_folder_infos();

            auto peer_folder_info = folder_infos_map.by_id(peer->get_id());
            if (!peer_folder_info)
                continue;

            auto &peer_file_infos = peer_folder_info->get_file_infos();
            f_peer_it = peer_file_infos.begin();
            f_peer_end = peer_file_infos.end();

            local_folder_info = folder_infos_map.by_id(cluster->get_device()->get_id());

            ++it_folder;
            break;
        }
    }

    while (f_peer_it != f_peer_end) {
        file = f_peer_it->second;
        ++f_peer_it;
        auto full_name = natural_key(file);
        auto local_file = local_folder_info->get_file_infos().by_id(full_name);
        if (!local_file) {
            return;
        }
        auto needs_update = !local_file->is_locked() &&
                            (local_file->is_older(*file) ||
                             (local_file->get_sequence() == file->get_sequence() && local_file->is_dirty()));
        if (needs_update) {
            return;
        }
    }
    goto TRY_ANEW;
}

void file_interator_t::reset() noexcept { cluster = nullptr; }

file_info_ptr_t file_interator_t::next() noexcept {
    file_info_ptr_t r = file->link(cluster->get_device());
    prepare();
    return r;
}

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
