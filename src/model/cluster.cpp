#include "cluster.h"
#include "misc/file_iterator.h"
#include "misc/block_iterator.h"
#include "diff/peer/cluster_update.h"
#include "diff/peer/update_folder.h"
#include <spdlog/spdlog.h>

using namespace syncspirit;
using namespace syncspirit::model;

cluster_t::cluster_t(device_ptr_t device_, size_t seed_) noexcept : device(device_), uuid_generator(rng_engine) {
    rng_engine.seed(seed_);
}

cluster_t::~cluster_t() {}

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

ignored_folders_map_t &cluster_t::get_ignored_folders() noexcept { return ignored_folders; }

block_infos_map_t &cluster_t::get_blocks() noexcept { return blocks; }

const block_infos_map_t &cluster_t::get_blocks() const noexcept { return blocks; }

folders_map_t &cluster_t::get_folders() noexcept { return folders; }

const folders_map_t &cluster_t::get_folders() const noexcept { return folders; }

uuid_t cluster_t::next_uuid() noexcept { return uuid_generator(); }

uint64_t cluster_t::next_uint64() noexcept { return uint64_generator(rng_engine); }

auto cluster_t::process(proto::ClusterConfig &msg, const device_t &peer) const noexcept
    -> outcome::result<diff::cluster_diff_ptr_t> {
    return diff::peer::cluster_update_t::create(*this, peer, msg);
}

auto cluster_t::process(proto::Index &msg, const device_t &peer) const noexcept
    -> outcome::result<diff::cluster_diff_ptr_t> {
    return diff::peer::update_folder_t::create(*this, peer, msg);
}

auto cluster_t::process(proto::IndexUpdate &msg, const device_t &peer) const noexcept
    -> outcome::result<diff::cluster_diff_ptr_t> {
    return diff::peer::update_folder_t::create(*this, peer, msg);
}

auto cluster_t::next_file(const device_ptr_t &peer, bool reset) noexcept -> file_info_ptr_t {
    assert(peer != device);
    if (reset) {
        file_iterator_map[peer] = new file_interator_t(*this, peer);
    }
    auto &it = file_iterator_map[peer];
    if (it && *it) {
        return it->next();
    }
    return {};
}

auto cluster_t::next_block(const file_info_ptr_t &source, bool reset) noexcept -> file_block_t {
    if (source->is_file() && !source->is_deleted()) {
        if (reset) {
            block_iterator_map[source] = new blocks_interator_t(*source);
        }
        auto &it = block_iterator_map[source];
        if (it && *it) {
            return it->next();
        }
    }
    return {};
}
