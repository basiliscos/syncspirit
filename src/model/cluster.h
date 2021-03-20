#pragma once

#include "../config/main.h"
#include "folder.h"
#include "device.h"
#include "arc.hpp"
#include <vector>
#include <unordered_map>

namespace syncspirit::model {

struct cluster_t : arc_base_t<cluster_t> {
    using unknown_folders_t = std::vector<proto::Folder>;

    cluster_t(device_ptr_t device_) noexcept;

    void assign_folders(folders_map_t &&folders) noexcept;
    proto::ClusterConfig get(model::device_ptr_t target) noexcept;
    unknown_folders_t update(const proto::ClusterConfig &config) noexcept;
    folder_ptr_t opt_for_synch(const device_ptr_t &peer_device) noexcept;

    const folders_map_t &get_folders() const noexcept;
    void add_folder(const folder_ptr_t &folder) noexcept;
#if 0

    cluster_t(device_ptr_t device_) noexcept;
    void sanitize(proto::Folder &folder, const devices_map_t &devices) noexcept;
    folder_ptr_t get_folder(const std::string &folder_id) noexcept;
    //folders_config_t serialize() noexcept;

    proto::ClusterConfig get() noexcept;

#endif
  private:
    device_ptr_t device;
    folders_map_t folders;
};

using cluster_ptr_t = intrusive_ptr_t<cluster_t>;

} // namespace syncspirit::model
