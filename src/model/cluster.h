#pragma once

#include <vector>
#include <unordered_map>
#include "arc.hpp"
#include "device.h"
#include "folder.h"
#include "block_info.h"

namespace syncspirit::model {

struct cluster_t : arc_base_t<cluster_t> {
    using unknown_folders_t = std::vector<proto::Folder>;

    cluster_t(device_ptr_t device_) noexcept;

    void assign_folders(const folders_map_t &folders) noexcept;
    void assign_blocks(block_infos_map_t &&blocks) noexcept;
    proto::ClusterConfig get(model::device_ptr_t target) noexcept;
    unknown_folders_t update(const proto::ClusterConfig &config) noexcept;
    file_info_ptr_t file_for_synch(const device_ptr_t &peer_device) noexcept;

    block_infos_map_t &get_blocks() noexcept;
    folders_map_t &get_folders() noexcept;
    void add_folder(const folder_ptr_t &folder) noexcept;

  private:
    device_ptr_t device;
    folders_map_t folders;
    block_infos_map_t blocks;
};

using cluster_ptr_t = intrusive_ptr_t<cluster_t>;

} // namespace syncspirit::model
