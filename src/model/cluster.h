#pragma once

#include <vector>
#include <random>
#include <boost/uuid/random_generator.hpp>
#include <unordered_map>
#include "misc/arc.hpp"
#include "misc/uuid.h"
#include "device.h"
#include "ignored_device.h"
#include "ignored_folder.h"
#include "folder.h"
#include "block_info.h"

namespace syncspirit::model {

struct file_interator_t;

#if 0
struct update_result_t {
    using unknown_folders_t = std::vector<const proto::Folder *>;
    using outdated_folders_t = std::unordered_set<const proto::Folder *>;

    unknown_folders_t unknown_folders;
    outdated_folders_t outdated_folders;
};
#endif

struct cluster_t : arc_base_t<cluster_t> {

    cluster_t(device_ptr_t device_, size_t seed) noexcept;

#if 0
    void assign_folders(const folders_map_t &folders) noexcept;
    void assign_blocks(block_infos_map_t &&blocks) noexcept;
    proto::ClusterConfig get(model::device_ptr_t target) noexcept;
#if 0
    update_result_t update(const proto::ClusterConfig &config) noexcept;
#endif
    void add_folder(const folder_ptr_t &folder) noexcept;
    file_interator_t iterate_files(const device_ptr_t &peer_device) noexcept;
#endif
    inline const device_ptr_t &get_device() const noexcept { return device; }
    block_infos_map_t &get_blocks() noexcept;
    devices_map_t &get_devices() noexcept;
    ignored_devices_map_t &get_ignored_devices() noexcept;
    ignored_folders_map_t &get_ignored_folders() noexcept;
    block_infos_map_t &get_deleted_blocks() noexcept;
    folders_map_t &get_folders() noexcept;
    uuid_t next_uuid() noexcept;

  private:
    using rng_engine_t = std::mt19937;
    using uuid_generator_t = boost::uuids::basic_random_generator<rng_engine_t>;

    rng_engine_t rng_engine;
    uuid_generator_t uuid_generator;
    device_ptr_t device;
    folders_map_t folders;
    block_infos_map_t blocks;
    block_infos_map_t deleted_blocks;
    devices_map_t devices;
    ignored_devices_map_t ignored_devices;
    ignored_folders_map_t ignored_folders;
    //folder_infos_map_t new_folder_infos;

    friend class file_interator_t;
};

using cluster_ptr_t = intrusive_ptr_t<cluster_t>;

} // namespace syncspirit::model
