#pragma once

#include <vector>
#include <unordered_map>
#include "arc.hpp"
#include "device.h"
#include "folder.h"
#include "block_info.h"

namespace syncspirit::model {

struct file_interator_t;

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
