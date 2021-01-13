#include "cluster.h"

using namespace syncspirit::model;

void cluster_t::add_folder(const folder_ptr_t &folder) noexcept { folders.emplace(folder->id, folder); }

cluster_t::folders_config_t cluster_t::serialize() noexcept {
    folders_config_t r;
    for (auto &[id, folder] : folders) {
        r.emplace(id, folder->serialize());
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
