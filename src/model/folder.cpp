#include "folder.h"
#include <spdlog.h>

using namespace syncspirit;
using namespace syncspirit::model;

folder_t::folder_t(const config::folder_config_t &cfg) noexcept
    : id{cfg.id}, label{cfg.label}, path{cfg.path}, folder_type{cfg.folder_type}, rescan_interval{cfg.rescan_interval},
      pull_order{cfg.pull_order}, watched{cfg.watched}, read_only{cfg.read_only},
      ignore_permissions{cfg.ignore_permissions}, ignore_delete{cfg.ignore_delete},
      disable_temp_indixes{cfg.disable_temp_indixes}, paused{cfg.paused} {}

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
        id,      label,     path.string(),      std::move(devices), folder_type,          rescan_interval, pull_order,
        watched, read_only, ignore_permissions, ignore_delete,      disable_temp_indixes, paused};
}

static proto::Compression compression(model::device_ptr_t device) noexcept {
    using C = proto::Compression;
    switch (device->compression) {
    case config::compression_t::none:
        return C::NEVER;
    case config::compression_t::meta:
        return C::METADATA;
    case config::compression_t::all:
        return C::ALWAYS;
    }
    return C::NEVER;
}

proto::Folder folder_t::get() noexcept {
    proto::Folder r;
    r.set_id(id);
    r.set_label(label);
    r.set_read_only(read_only);
    r.set_ignore_permissions(ignore_permissions);
    r.set_ignore_delete(ignore_delete);
    r.set_disable_temp_indexes(disable_temp_indixes);
    r.set_paused(paused);
    int i = 0;
    for (auto &fd : this->devices) {
        auto &id = fd.device->device_id.get_value();
        proto::Device pd;
        auto &device = fd.device;
        pd.set_id(id);
        pd.set_name(device->name);
        pd.set_compression(compression(device));
        if (device->cert_name)
            pd.set_cert_name(device->cert_name.value());
        pd.set_max_sequence(fd.max_sequence);
        pd.set_introducer(device->introducer);
        pd.set_index_id(fd.index_id);
        pd.set_skip_introduction_removals(device->skip_introduction_removals);
        *r.add_devices() = pd;
    }
    return r;
}
