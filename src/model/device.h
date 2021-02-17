#pragma once

#include <unordered_map>
#include "arc.hpp"
#include "device_id.h"
#include "../utils/uri.h"
#include "../config/device.h"

namespace syncspirit::model {

struct device_t : arc_base_t<device_t> {
    using static_addresses_t = std::vector<utils::URI>;

    device_t(config::device_config_t &config) noexcept;

    device_id_t device_id;
    std::string name;
    config::compression_t compression;
    std::optional<std::string> cert_name;
    static_addresses_t static_addresses;
    bool introducer;
    bool auto_accept;
    bool paused;
    bool skip_introduction_removals;
    config::device_config_t::ignored_folders_t ignored_folders;
    bool online = false;

    bool operator==(const device_t &other) const noexcept { return other.device_id == device_id; }
    bool operator!=(const device_t &other) const noexcept { return other.device_id != device_id; }

    config::device_config_t serialize() noexcept;
    inline bool is_dynamic() const noexcept { return static_addresses.empty(); }
    void mark_online(bool value) noexcept;
};

using device_ptr_t = intrusive_ptr_t<device_t>;

using devices_map_t = std::unordered_map<std::string, device_ptr_t>;

} // namespace syncspirit::model

namespace std {

template <> struct hash<syncspirit::model::device_t> {
    inline size_t operator()(const syncspirit::model::device_t &device) const noexcept {
        return std::hash<syncspirit::model::device_id_t>()(device.device_id);
    }
};

template <> struct hash<syncspirit::model::device_ptr_t> {
    inline size_t operator()(const syncspirit::model::device_ptr_t &device) const noexcept {
        return std::hash<syncspirit::model::device_t>()(*device);
    }
};

} // namespace std
