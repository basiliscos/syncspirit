#include "folder_info.h"
#include "folder.h"
#include <spdlog.h>

namespace syncspirit::model {

folder_info_t::folder_info_t(const db::FolderInfo &info_, device_t *device_, folder_t *folder_,
                             uint64_t db_key_) noexcept
    : index{info_.index_id()}, max_sequence{0}, device{device_}, folder{folder_}, db_key{db_key_} {
    assert(device);
    assert(folder);
}

folder_info_t::~folder_info_t() {}

db::FolderInfo folder_info_t::serialize() noexcept {
    db::FolderInfo r;
    r.set_index_id(index);
    auto device_key = device->get_db_key();
    auto folder_key = folder->get_db_key();
    r.set_device_key(device_key);
    r.set_folder_key(folder_key);
    return r;
}

} // namespace syncspirit::model
