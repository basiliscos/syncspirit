#pragma once

#include <cstdint>
#include <set>
#include <boost/filesystem.hpp>
#include "device.h"
#include "../config/folder.h"
#include "bep.pb.h"

namespace syncspirit::model {

namespace fs = boost::filesystem;

using index_id_t = std::uint64_t;
using sequence_id_t = std::int64_t;

struct folder_device_t {
    device_ptr_t device;
    index_id_t index_id;
    sequence_id_t max_sequence;
};

} // namespace syncspirit::model

namespace std {
template <> struct less<syncspirit::model::folder_device_t> {
    using fd_t = syncspirit::model::folder_device_t;
    bool operator()(const fd_t &lhs, const fd_t &rhs) const noexcept {
        auto r = lhs.device->device_id.get_sha256().compare(rhs.device->device_id.get_sha256());
        return r;
    }
};
} // namespace std

namespace syncspirit::model {

struct folder_t : arc_base_t<folder_t> {
    using devices_t = std::set<folder_device_t>;

    folder_t(const config::folder_config_t &cfg, const device_ptr_t &device_) noexcept;

    bool assign(const proto::Folder &source, const devices_map_t &devices) noexcept;
    config::folder_config_t serialize(device_ptr_t local_device) noexcept;

    proto::Folder get() noexcept;
    std::int64_t score(const device_ptr_t &peer_device) noexcept;

    // from config
    std::string id;
    std::string label;
    fs::path path;
    devices_t devices;
    config::folder_type_t folder_type;
    std::uint32_t rescan_interval;
    config::pull_order_t pull_order;
    bool watched;
    bool read_only;
    bool ignore_permissions;
    bool ignore_delete;
    bool disable_temp_indixes;
    bool paused;
    //
    device_ptr_t device;
};

using folder_ptr_t = intrusive_ptr_t<folder_t>;

} // namespace syncspirit::model
