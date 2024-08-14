// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include "misc/arc.hpp"
#include "misc/uuid.h"
#include "device.h"
#include "ignored_device.h"
#include "ignored_folder.h"
#include "folder.h"
#include "unknown_device.h"
#include "unknown_folder.h"
#include "block_info.h"

#include <random>
#include <boost/uuid/random_generator.hpp>

namespace syncspirit::model {

struct SYNCSPIRIT_API cluster_t final : arc_base_t<cluster_t> {

    cluster_t(device_ptr_t device_, size_t seed, int32_t write_requests) noexcept;

    proto::ClusterConfig generate(const model::device_t &target) const noexcept;
    inline const device_ptr_t &get_device() const noexcept { return device; }
    block_infos_map_t &get_blocks() noexcept;
    const block_infos_map_t &get_blocks() const noexcept;
    devices_map_t &get_devices() noexcept;
    const devices_map_t &get_devices() const noexcept;
    const ignored_devices_map_t &get_ignored_devices() const noexcept;
    ignored_devices_map_t &get_ignored_devices() noexcept;
    ignored_folders_map_t &get_ignored_folders() noexcept;
    folders_map_t &get_folders() noexcept;
    unknown_devices_map_t &get_unknown_devices() noexcept;
    const unknown_devices_map_t &get_unknown_devices() const noexcept;
    unknown_folder_map_t &get_unknown_folders() noexcept;

    const folders_map_t &get_folders() const noexcept;
    const unknown_folder_map_t &get_unknown_folders() const noexcept;
    uuid_t next_uuid() noexcept;
    inline bool is_tainted() const noexcept { return tainted; }
    inline void mark_tainted() noexcept { tainted = true; }
    int32_t get_write_requests() const noexcept;
    void modify_write_requests(int32_t delta) noexcept;

  private:
    using rng_engine_t = std::mt19937;
    using uuid_generator_t = boost::uuids::basic_random_generator<rng_engine_t>;
    using uint64_generator_t = std::uniform_int_distribution<uint64_t>;

    device_ptr_t device;
    rng_engine_t rng_engine;
    uuid_generator_t uuid_generator;
    uint64_generator_t uint64_generator;
    folders_map_t folders;
    block_infos_map_t blocks;
    devices_map_t devices;
    ignored_devices_map_t ignored_devices;
    ignored_folders_map_t ignored_folders;
    unknown_folder_map_t unknown_folders;
    unknown_devices_map_t unknown_devices;
    bool tainted = false;
    int32_t write_requests;
};

using cluster_ptr_t = intrusive_ptr_t<cluster_t>;

} // namespace syncspirit::model
