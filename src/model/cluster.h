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
#include "diff/cluster_diff.h"

namespace syncspirit::model {

struct file_interator_t;

struct cluster_t final : arc_base_t<cluster_t> {

    cluster_t(device_ptr_t device_, size_t seed) noexcept;

    proto::ClusterConfig generate(const model::device_t& target) const noexcept;
#if 0
    file_interator_t iterate_files(const device_ptr_t &peer_device) noexcept;
#endif
    inline const device_ptr_t &get_device() const noexcept { return device; }
    block_infos_map_t &get_blocks() noexcept;
    const block_infos_map_t &get_blocks() const noexcept;
    devices_map_t &get_devices() noexcept;
    const devices_map_t &get_devices() const noexcept;
    ignored_devices_map_t &get_ignored_devices() noexcept;
    ignored_folders_map_t &get_ignored_folders() noexcept;
    block_infos_map_t &get_deleted_blocks() noexcept;
    folders_map_t &get_folders() noexcept;
    const folders_map_t &get_folders() const noexcept;
    uuid_t next_uuid() noexcept;
    uint64_t next_uint64() noexcept;
    inline bool is_tainted() const noexcept { return tainted; }
    inline void mark_tainted() noexcept { tainted = true; }

    outcome::result<diff::cluster_diff_ptr_t> process(proto::ClusterConfig& msg, const device_t& peer) const noexcept;

  private:
    using rng_engine_t = std::mt19937;
    using uuid_generator_t = boost::uuids::basic_random_generator<rng_engine_t>;
    using uint64_generator_t = std::uniform_int_distribution<uint64_t>;

    rng_engine_t rng_engine;
    uuid_generator_t uuid_generator;
    uint64_generator_t uint64_generator;
    device_ptr_t device;
    folders_map_t folders;
    block_infos_map_t blocks;
    block_infos_map_t deleted_blocks;
    devices_map_t devices;
    ignored_devices_map_t ignored_devices;
    ignored_folders_map_t ignored_folders;
    bool tainted = false;

    friend class file_interator_t;
};

using cluster_ptr_t = intrusive_ptr_t<cluster_t>;

} // namespace syncspirit::model
