// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "cluster.h"
#include <spdlog/spdlog.h>

using namespace syncspirit;
using namespace syncspirit::model;

cluster_t::cluster_t(device_ptr_t device_, int32_t write_requests_) noexcept
    : device(device_), tainted{false}, write_requests{write_requests_} {}

proto::ClusterConfig cluster_t::generate(const device_t &target) const noexcept {
    proto::ClusterConfig r;
    for (auto it : folders) {
        auto &folder = it.item;
        auto folder_opt = folder->generate(target);
        if (folder_opt) {
            *(r.add_folders()) = folder_opt.value();
        }
    }
    return r;
}

devices_map_t &cluster_t::get_devices() noexcept { return devices; }

const devices_map_t &cluster_t::get_devices() const noexcept { return devices; }

ignored_devices_map_t &cluster_t::get_ignored_devices() noexcept { return ignored_devices; }

const ignored_devices_map_t &cluster_t::get_ignored_devices() const noexcept { return ignored_devices; }

ignored_folders_map_t &cluster_t::get_ignored_folders() noexcept { return ignored_folders; }

block_infos_map_t &cluster_t::get_blocks() noexcept { return blocks; }

const block_infos_map_t &cluster_t::get_blocks() const noexcept { return blocks; }

folders_map_t &cluster_t::get_folders() noexcept { return folders; }

auto cluster_t::get_pending_devices() noexcept -> pending_devices_map_t & { return pending_devices; }

auto cluster_t::get_pending_devices() const noexcept -> const pending_devices_map_t & { return pending_devices; }

auto cluster_t::get_pending_folders() noexcept -> pending_folder_map_t & { return pending_folders; }

auto cluster_t::get_pending_folders() const noexcept -> const pending_folder_map_t & { return pending_folders; }

const folders_map_t &cluster_t::get_folders() const noexcept { return folders; }

int32_t cluster_t::get_write_requests() const noexcept { return write_requests; }

void cluster_t::modify_write_requests(int32_t delta) noexcept {
    write_requests += delta;
    assert(write_requests >= 0);
}
