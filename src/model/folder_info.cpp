#include "folder_info.h"
#include "folder.h"
#include <spdlog.h>

namespace syncspirit::model {

folder_info_t::folder_info_t(const db::FolderInfo &info_, device_t *device_, folder_t *folder_,
                             uint64_t db_key_) noexcept
    : index{info_.index_id()}, max_sequence{info_.max_sequence()}, device{device_}, folder{folder_}, db_key{db_key_} {
    assert(device);
    assert(folder);
}

folder_info_t::~folder_info_t() {}

void folder_info_t::add(const file_info_ptr_t &file_info) noexcept {
    file_infos.put(file_info);
    mark_dirty();
}

void folder_info_t::update(const proto::Device &device) noexcept {
    bool changed = false;
    if (index != device.index_id()) {
        index = device.index_id();
        changed = true;
    }
    if (max_sequence != device.max_sequence()) {
        max_sequence = device.max_sequence();
        changed = true;
    }
    if (changed) {
        spdlog::trace("folder_info_t::update, folder = {}, index = {:#x}, max seq = {}", folder->label(), index,
                      max_sequence);
        mark_dirty();
    }
}

std::int64_t folder_info_t::inc_max_sequence() noexcept {
    mark_dirty();
    return ++max_sequence;
}

db::FolderInfo folder_info_t::serialize() noexcept {
    db::FolderInfo r;
    r.set_index_id(index);
    auto device_key = device->get_db_key();
    auto folder_key = folder->get_db_key();
    assert(device_key && "device have to be persisted first");
    assert(folder_key && "folder have to be persisted first");
    r.set_device_key(device_key);
    r.set_folder_key(folder_key);
    r.set_max_sequence(max_sequence);
    return r;
}

template <typename Message> void folder_info_t::update_generic(const Message &data, const device_ptr_t &peer) noexcept {
    std::int64_t max_sequence = get_max_sequence();
    for (int i = 0; i < data.files_size(); ++i) {
        auto &file = data.files(i);
        auto seq = file.sequence();
        spdlog::trace("folder_info_t::update, folder = {}, device = {}, file = {}, seq = {}", folder->label(),
                      device->device_id, file.name(), seq);

        auto file_key = file_info_t::generate_db_key(file.name(), *this);
        auto fi = file_infos.by_key(file_key);
        if (fi) {
            fi->update(file);
        } else {
            fi = file_info_ptr_t(new file_info_t(file, this));
            add(fi);
            mark_dirty();
        }
        if (seq > max_sequence) {
            max_sequence = seq;
        }
    }
    if (get_max_sequence() < max_sequence) {
        this->max_sequence = max_sequence;
        mark_dirty();
    }

    spdlog::debug("folder_info_t::update, folder_info = {} max seq = {}, device = {}", get_db_key(), max_sequence,
                  peer->device_id);
    /*
    auto local_folder_info = folder_infos.by_id(device->device_id.get_sha256());
    if (local_folder_info->get_max_sequence() < max_sequence) {
        local_folder_info->set_max_sequence(max_sequence);
        spdlog::trace("folder_t::update, folder_info = {} max seq = {}, device = {} (local)",
                      local_folder_info->get_db_key(), max_sequence, device->device_id);
    }
    */
}

void folder_info_t::update(const proto::IndexUpdate &data, const device_ptr_t &peer) noexcept {
    update_generic(data, peer);
}

void folder_info_t::update(const proto::Index &data, const device_ptr_t &peer) noexcept { update_generic(data, peer); }

void folder_info_t::update(local_file_map_t &local_files) noexcept {
    auto file_infos_copy = file_infos;
    for (auto it : local_files.map) {
        auto file_key = file_info_t::generate_db_key(it.first.string(), *this);
        auto cluster_file = file_infos_copy.by_key(file_key);
        if (cluster_file) {
            auto updated = cluster_file->update(it.second);
            file_infos_copy.remove(cluster_file);
            if (updated) {
                std::abort();
            }
        }
    }
    for (auto it : file_infos_copy) {
        auto &file = it.second;
        if (file->is_deleted()) {
            // no-op, file is deleted in local index and does not present in filesystem
        } else {
            std::abort();
        }
    }
}

} // namespace syncspirit::model
