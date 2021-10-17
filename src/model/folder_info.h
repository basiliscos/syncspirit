#pragma once

#include <cstdint>
#include "device.h"
#include "file_info.h"
#include "misc/local_file.h"
#include "misc/storeable.h"

namespace syncspirit::model {

struct folder_t;

struct folder_info_t : arc_base_t<folder_info_t>, storeable_t {
    folder_info_t(const db::FolderInfo &info_, device_t *device_, folder_t *folder_, std::string_view key) noexcept;
    folder_info_t(const db::FolderInfo &info_, device_t *device_, folder_t *folder_) noexcept;
    ~folder_info_t();

    std::string_view get_key() noexcept;

    bool operator==(const folder_info_t &other) const noexcept;
    bool operator!=(const folder_info_t &other) const noexcept { return !(*this == other); }

    void add(const file_info_ptr_t &file_info) noexcept;
    db::FolderInfo serialize() noexcept;

    inline std::uint64_t get_index() const noexcept { return index; }

    inline device_t *get_device() const noexcept { return device; }
    inline folder_t *get_folder() const noexcept { return folder; }
    inline std::int64_t get_max_sequence() const noexcept { return max_sequence; }
    void update(const proto::Index &data, const device_ptr_t &peer) noexcept;
    void update(const proto::IndexUpdate &data, const device_ptr_t &peer) noexcept;
    bool update(const proto::Device &device) noexcept;
    void update(local_file_map_t &local_files) noexcept;
    void remove() noexcept;
    void set_max_sequence(std::int64_t value) noexcept;
    inline file_infos_map_t &get_file_infos() noexcept { return file_infos; }

  private:
    template <typename Message> void update_generic(const Message &data, const device_ptr_t &peer) noexcept;
    static const constexpr auto data_length = uuid_length * 2 + device_id_t::data_length;

    char uuid[data_length];

    std::uint64_t index;
    std::int64_t max_sequence;
    device_t *device;
    folder_t *folder;
    file_infos_map_t file_infos;
};

using folder_info_ptr_t = intrusive_ptr_t<folder_info_t>;

#if 0
inline const std::string &natural_key(const folder_info_ptr_t &item) noexcept { return item->get_device()->get_id(); }
inline std::uint64_t db_key(const folder_info_ptr_t &item) noexcept { return item->get_db_key(); }
#endif

struct folder_infos_map_t: public generic_map_t<folder_info_ptr_t, 2> {
    folder_info_ptr_t byDevice(const device_ptr_t& device) {
        return get<1>(device->get_key());
    }
};

}; // namespace syncspirit::model
