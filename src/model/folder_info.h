#pragma once

#include <cstdint>
#include "device.h"
#include "file_info.h"
#include "misc/local_file.h"
#include "misc/storeable.h"
#include "structs.pb.h"
#include <boost/outcome.hpp>

namespace syncspirit::model {

namespace outcome = boost::outcome_v2;

struct folder_t;
using folder_ptr_t = intrusive_ptr_t<folder_t>;

struct folder_info_t;
using folder_info_ptr_t = intrusive_ptr_t<folder_info_t>;

struct folder_info_t final : arc_base_t<folder_info_t>, storeable_t {

    static outcome::result<folder_info_ptr_t> create(std::string_view key, const db::FolderInfo& data, const device_ptr_t& device_, const folder_ptr_t& folder_) noexcept;
    static outcome::result<folder_info_ptr_t> create(const uuid_t& uuid, const db::FolderInfo& data, const device_ptr_t& device_, const folder_ptr_t& folder_) noexcept;
    ~folder_info_t();

    std::string_view get_key() noexcept;
    std::string_view get_uuid() noexcept;

    bool operator==(const folder_info_t &other) const noexcept;
    bool operator!=(const folder_info_t &other) const noexcept { return !(*this == other); }

    void add(const file_info_ptr_t &file_info) noexcept;
    std::string serialize() noexcept;

    inline std::uint64_t get_index() const noexcept { return index; }

    inline device_t *get_device() const noexcept { return device; }
    inline folder_t *get_folder() const noexcept { return folder; }
    inline std::int64_t get_max_sequence() const noexcept { return max_sequence; }
    void update(const proto::Index &data, const device_ptr_t &peer) noexcept;
    void update(const proto::IndexUpdate &data, const device_ptr_t &peer) noexcept;
    //bool update(const proto::Device &device) noexcept;
    void update(local_file_map_t &local_files) noexcept;
    void remove() noexcept;
    void set_max_sequence(std::int64_t value) noexcept;
    inline file_infos_map_t &get_file_infos() noexcept { return file_infos; }

  private:
    folder_info_t(std::string_view key, const device_ptr_t& device_, const folder_ptr_t& folder_) noexcept;
    folder_info_t(const uuid_t& uuid, const device_ptr_t& device_, const folder_ptr_t& folder_) noexcept;
    void assign_fields(const db::FolderInfo& data) noexcept;

    template <typename Message> void update_generic(const Message &data, const device_ptr_t &peer) noexcept;

    static const constexpr auto data_length = uuid_length * 2 + device_id_t::digest_length + 1;

    char key[data_length];

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
    folder_info_ptr_t by_device(const device_ptr_t& device) const noexcept;
    folder_info_ptr_t by_device_id(std::string_view device_id) const noexcept;
};

}; // namespace syncspirit::model
