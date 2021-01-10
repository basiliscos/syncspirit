#include "folder.h"
#include <spdlog.h>

using namespace syncspirit::model;

folder_t::folder_t(const config::folder_config_t &cfg) noexcept
    : id{cfg.id}, label{cfg.label}, path{cfg.path}, folder_type{cfg.folder_type}, rescan_interval{cfg.rescan_interval},
      pull_order{cfg.pull_order}, watched{cfg.watched}, ignore_permissions{cfg.ignore_permissions} {}

void folder_t::assign(const proto::Folder &source, const devices_map_t &devices_map) noexcept {
    for (int i = 0; i < source.devices_size(); ++i) {
        auto &d = source.devices(i);
        auto &device_id = d.id();
        auto it = devices_map.find(device_id);
        if (it == devices_map.end()) {
            spdlog::warn("load_folder, unknown device {}, ignoring", device_id);
        } else {
            devices.emplace(model::folder_device_t{it->second, d.index_id(), d.max_sequence()});
        }
    }
}
