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

void folder_info_t::update(const proto::Device &device) noexcept {
    bool changed = false;
    if (index != device.index_id()) {
        index = device.index_id();
        changed = true;
    }
    if (changed) {
        spdlog::trace("folder_info_t::update, folder = {}, index = {:#x}, max seq = {}", folder->label(), index,
                      max_sequence);
        mark_dirty();
    }
}

void folder_info_t::update_max_sequence(std::int64_t value) noexcept {
    max_sequence = value;
    mark_dirty();
}

db::FolderInfo folder_info_t::serialize() noexcept {
    db::FolderInfo r;
    r.set_index_id(index);
    auto device_key = device->get_db_key();
    auto folder_key = folder->get_db_key();
    r.set_device_key(device_key);
    r.set_folder_key(folder_key);
    r.set_max_sequence(max_sequence);
    return r;
}

} // namespace syncspirit::model
