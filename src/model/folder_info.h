#pragma once

#include <cstdint>
#include "device.h"
#include "file_info.h"
#include "structs.pb.h"

namespace syncspirit::model {

struct folder_t;

struct folder_info_t : arc_base_t<folder_info_t> {
    folder_info_t(const db::FolderInfo &info_, device_t *device_, folder_t *folder_, std::uint64_t db_key_) noexcept;
    ~folder_info_t();

    bool operator==(const folder_info_t &other) const noexcept { return other.db_key == db_key; }
    bool operator!=(const folder_info_t &other) const noexcept { return other.db_key != db_key; }

    void add(file_info_ptr_t &file_info) noexcept;

    db::FolderInfo serialize() noexcept;

    inline std::uint64_t get_db_key() const noexcept { return db_key; }
    inline void set_db_key(std::uint64_t value) noexcept { db_key = value; }

    inline device_t *get_device() const noexcept { return device; }
    inline folder_t *get_folder() const noexcept { return folder; }
    inline std::int64_t get_max_sequence() const noexcept { return max_sequence; }

  private:
    std::uint64_t index;
    std::int64_t max_sequence;
    device_t *device;
    folder_t *folder;
    std::uint64_t db_key;
    file_infos_map_t file_infos;
};

using folder_info_ptr_t = intrusive_ptr_t<folder_info_t>;

inline std::uint64_t db_key(const folder_info_ptr_t &item) noexcept { return item->get_db_key(); }

using folder_infos_map_t = generic_map_t<folder_info_ptr_t, void>;

}; // namespace syncspirit::model
