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
    inline bool operator()(const fd_t &lhs, const fd_t &rhs) const noexcept {
        return lhs.device->device_id < rhs.device->device_id;
    }
};
} // namespace std

namespace syncspirit::model {

struct folder_t : arc_base_t<folder_t> {
    using devices_t = std::set<folder_device_t>;

    folder_t(const config::folder_config_t &cfg) noexcept;

    void assign(const proto::Folder &source, const devices_map_t &devices) noexcept;
    // from config
    std::string id;
    std::string label;
    fs::path path;
    devices_t devices;
    config::folder_type_t folder_type;
    std::uint32_t rescan_interval;
    config::pull_order_t pull_order;
    bool watched;
    bool ignore_permissions;
};

using folder_ptr_t = intrusive_ptr_t<folder_t>;

} // namespace syncspirit::model
