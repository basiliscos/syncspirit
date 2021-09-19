#pragma once

#include <cstdint>
#include "device.h"
#include "file_info.h"
#include "local_file.h"
#include "structs.pb.h"
#include "storeable.h"

namespace syncspirit::model {

struct folder_t;

struct folder_info_t : arc_base_t<folder_info_t>, storeable_t {
    folder_info_t(const db::FolderInfo &info_, device_t *device_, folder_t *folder_, std::uint64_t db_key_) noexcept;
    ~folder_info_t();

    bool operator==(const folder_info_t &other) const noexcept { return other.db_key == db_key; }
    bool operator!=(const folder_info_t &other) const noexcept { return other.db_key != db_key; }

    void add(const file_info_ptr_t &file_info) noexcept;
    db::FolderInfo serialize() noexcept;

    inline std::uint64_t get_index() const noexcept { return index; }
    inline std::uint64_t get_db_key() const noexcept { return db_key; }
    inline void set_db_key(std::uint64_t value) noexcept { db_key = value; }

    inline device_t *get_device() const noexcept { return device; }
    inline folder_t *get_folder() const noexcept { return folder; }
    inline std::int64_t get_max_sequence() const noexcept { return max_sequence; }
    void update(const proto::Index &data, const device_ptr_t &peer) noexcept;
    void update(const proto::IndexUpdate &data, const device_ptr_t &peer) noexcept;
    bool update(const proto::Device &device) noexcept;
    void update(local_file_map_t &local_files) noexcept;
    void set_max_sequence(std::int64_t value) noexcept;
    inline file_infos_map_t &get_file_infos() noexcept { return file_infos; }

  private:
    template <typename Message> void update_generic(const Message &data, const device_ptr_t &peer) noexcept;
    std::uint64_t index;
    std::int64_t max_sequence;
    device_t *device;
    folder_t *folder;
    std::uint64_t db_key;
    file_infos_map_t file_infos;
};

using folder_info_ptr_t = intrusive_ptr_t<folder_info_t>;

inline const std::string &natural_key(const folder_info_ptr_t &item) noexcept { return item->get_device()->get_id(); }
inline std::uint64_t db_key(const folder_info_ptr_t &item) noexcept { return item->get_db_key(); }

using folder_infos_map_t = generic_map_t<folder_info_ptr_t, std::string>;

}; // namespace syncspirit::model
