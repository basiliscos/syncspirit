#pragma once

#include <vector>
#include <unordered_map>
#include "arc.hpp"
#include "device.h"
#include "folder.h"
#include "block_info.h"

namespace syncspirit::model {

struct cluster_t;

struct file_interator_t {
    file_interator_t() noexcept;
    file_interator_t(cluster_t &cluster, const device_ptr_t &peer) noexcept;

    template <typename T> file_interator_t &operator=(T &other) noexcept {
        cluster = other.cluster;
        peer = other.peer;
        it_folder = other.it_folder;
        f_peer_it = other.f_begin;
        f_peer_end = other.f_end;
        file = other.file;
        return *this;
    }

    inline operator bool() noexcept { return cluster != nullptr; }

    file_info_ptr_t next() noexcept;
    void reset() noexcept;

  private:
    using it_folder_t = typename folders_map_t::iterator;
    using it_file_t = typename file_infos_map_t::iterator;

    void prepare() noexcept;

    cluster_t *cluster;
    device_ptr_t peer;
    folder_info_ptr_t local_folder_info;
    it_folder_t it_folder;
    it_file_t f_peer_it;
    it_file_t f_peer_end;
    it_file_t f_local_it;
    it_file_t f_local_end;
    file_info_ptr_t file;
};

struct update_result_t {
    using unknown_folders_t = std::vector<const proto::Folder *>;
    using outdated_folders_t = std::unordered_set<const proto::Folder *>;

    unknown_folders_t unknown_folders;
    outdated_folders_t outdated_folders;
};

struct cluster_t : arc_base_t<cluster_t> {

    cluster_t(device_ptr_t device_) noexcept;

    void assign_folders(const folders_map_t &folders) noexcept;
    void assign_blocks(block_infos_map_t &&blocks) noexcept;
    proto::ClusterConfig get(model::device_ptr_t target) noexcept;
    update_result_t update(const proto::ClusterConfig &config) noexcept;
    file_interator_t iterate_files(const device_ptr_t &peer_device) noexcept;

    block_infos_map_t &get_blocks() noexcept;
    block_infos_map_t &get_deleted_blocks() noexcept;
    folders_map_t &get_folders() noexcept;
    void add_folder(const folder_ptr_t &folder) noexcept;
    inline const device_ptr_t &get_device() const noexcept { return device; }

  private:
    device_ptr_t device;
    folders_map_t folders;
    block_infos_map_t blocks;
    block_infos_map_t deleted_blocks;

    friend class file_interator_t;
};

using cluster_ptr_t = intrusive_ptr_t<cluster_t>;

} // namespace syncspirit::model
