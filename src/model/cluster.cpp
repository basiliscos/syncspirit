#include "cluster.h"

using namespace syncspirit::model;

cluster_t::cluster_t(device_ptr_t device_) noexcept : device(std::move(device_)) {}

void cluster_t::add_folder(const folder_ptr_t &folder) noexcept { folders.emplace(folder->id, folder); }

cluster_t::folders_config_t cluster_t::serialize() noexcept {
    folders_config_t r;
    for (auto &[id, folder] : folders) {
        r.emplace(id, folder->serialize(device));
    }
    return r;
}

folder_ptr_t cluster_t::get_folder(const std::string &folder_id) noexcept {
    auto it = folders.find(folder_id);
    if (it != folders.end()) {
        return it->second;
    }
    return folder_ptr_t{};
}

void cluster_t::sanitize(proto::Folder &folder, const devices_map_t &devices) noexcept {
    auto copy = folder;
    folder.clear_devices();
    for (int i = 0; i < copy.devices_size(); ++i) {
        auto &fd = copy.devices(i);
        auto &id = fd.id();
        auto predicate = [&id](auto &it) { return it.second->device_id.get_sha256() == id; };
        bool append = (id != device->device_id.get_sha256()) &&
                      std::find_if(devices.begin(), devices.end(), predicate) != devices.end();
        if (append) {
            *folder.add_devices() = fd;
        }
    }
}
