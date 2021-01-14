#include "folder.h"
#include <spdlog.h>

using namespace syncspirit;
using namespace syncspirit::model;

folder_t::folder_t(const config::folder_config_t &cfg) noexcept
    : id{cfg.id}, label{cfg.label}, path{cfg.path}, folder_type{cfg.folder_type}, rescan_interval{cfg.rescan_interval},
      pull_order{cfg.pull_order}, watched{cfg.watched}, ignore_permissions{cfg.ignore_permissions} {}

bool folder_t::assign(const proto::Folder &source, const devices_map_t &devices_map) noexcept {
    // remove outdated
    bool changed = false;
    for (auto it = devices.begin(); it != devices.end();) {
        bool has = false;
        for (int i = 0; i < source.devices_size(); ++i) {
            if (it->device->device_id.get_sha256() == source.devices(i).id()) {
                has = true;
                break;
            }
        }
        if (!has) {
            it = devices.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }
    // append possibly new
    for (int i = 0; i < source.devices_size(); ++i) {
        auto &d = source.devices(i);
        auto &raw_id = d.id();
        auto device_id_option = device_id_t::from_sha256(raw_id);
        if (!device_id_option) {
            spdlog::warn("load_folder, cannot obtain device id from digest: {}", raw_id);
            continue;
        }
        auto &device_id = device_id_option.value().get_value();
        auto it = devices_map.find(device_id);
        if (it == devices_map.end()) {
            spdlog::warn("load_folder, unknown device {}, ignoring", device_id);
            continue;
        }
        auto r = devices.emplace(model::folder_device_t{it->second, d.index_id(), d.max_sequence()});
        changed |= r.second;
    }
    return changed;
}

config::folder_config_t folder_t::serialize(device_ptr_t local_device) noexcept {
    config::folder_config_t::device_ids_t devices;
    for (auto &fd : this->devices) {
        auto &id = fd.device->device_id.get_value();
        if (id != local_device->device_id.get_value()) {
            devices.insert(id);
        }
    }
    return config::folder_config_t{
        id,         label,   path.string(),     std::move(devices), folder_type, rescan_interval,
        pull_order, watched, ignore_permissions};
}
