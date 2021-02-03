#pragma once

#include "../config/main.h"
#include "folder.h"
#include "device.h"
#include "arc.hpp"
#include <vector>
#include <unordered_map>

namespace syncspirit::model {

struct cluster_t : arc_base_t<cluster_t> {
    using folders_t = std::unordered_map<std::string, folder_ptr_t>;
    using unknown_folders_t = std::vector<proto::Folder>;
    using folders_config_t = config::main_t::folders_t;

    cluster_t(device_ptr_t device_) noexcept;
    void add_folder(const folder_ptr_t &folder) noexcept;
    unknown_folders_t update(proto::ClusterConfig &config, const devices_map_t &devices) noexcept;
    void sanitize(proto::Folder &folder, const devices_map_t &devices) noexcept;
    folder_ptr_t get_folder(const std::string &folder_id) noexcept;
    folders_config_t serialize() noexcept;
    proto::ClusterConfig get() noexcept;
    folder_ptr_t opt_for_synch(const device_ptr_t &peer_device) noexcept;

    device_ptr_t device;
    folders_t folders;
};

using cluster_ptr_t = intrusive_ptr_t<cluster_t>;

} // namespace syncspirit::model
