#pragma once

#include <cstdint>
#include <vector>
#include <optional>
#include <boost/filesystem.hpp>
#include <boost/outcome.hpp>
#include "misc/arc.hpp"
#include "misc/map.hpp"
#include "misc/storeable.h"
#include "misc/uuid.h"
#include "block_info.h"
#include "device.h"
#include "structs.pb.h"

namespace syncspirit::model {

namespace bfs = boost::filesystem;
namespace outcome = boost::outcome_v2;

struct folder_info_t;
using folder_info_ptr_t = intrusive_ptr_t<folder_info_t>;

struct local_file_t;
struct blocks_interator_t;

struct file_info_t;
using file_info_ptr_t = intrusive_ptr_t<file_info_t>;


struct file_info_t final : arc_base_t<file_info_t>, storeable_t {
    using blocks_t = std::vector<block_info_ptr_t>;

    static outcome::result<file_info_ptr_t> create(std::string_view key, const db::FileInfo& data, const folder_info_ptr_t& folder_info_) noexcept;
    static outcome::result<file_info_ptr_t> create(const uuid_t& uuid, const proto::FileInfo &info_, const folder_info_ptr_t& folder_info_) noexcept;
    static std::string create_key(const uuid_t& uuid, const folder_info_ptr_t& folder_info_) noexcept;

    ~file_info_t();

    std::string_view get_key() const noexcept { return std::string_view(key, data_length); }
    std::string_view get_uuid() const noexcept { return std::string_view(key + 1, uuid_length); }
    bool operator==(const file_info_t &other) const noexcept { return get_uuid() == other.get_uuid(); }

    std::string serialize(bool include_blocks = true) noexcept;

    void add_block(const block_info_ptr_t& block) noexcept;

    void update(const proto::FileInfo &remote_info) noexcept;
    bool update(const local_file_t &local_file) noexcept;

    inline folder_info_t *get_folder_info() const noexcept { return folder_info; }
    std::string_view get_name() const noexcept;
    inline const std::string &get_full_name() const noexcept { return full_name; }

    inline std::int64_t get_sequence() const noexcept { return sequence; }
    inline blocks_t &get_blocks() noexcept { return blocks; }

    void remove_blocks() noexcept;
    void append_block(const model::block_info_ptr_t &block, size_t index) noexcept;

    inline bool is_file() noexcept { return type == proto::FileInfoType::FILE; }
    inline bool is_dir() noexcept { return type == proto::FileInfoType::DIRECTORY; }
    inline bool is_link() noexcept { return type == proto::FileInfoType::SYMLINK; }
    inline bool is_deleted() noexcept { return deleted; }

    //static std::string generate_db_key(const std::string &name, const folder_info_t &folder) noexcept;

    inline std::int64_t get_size() const noexcept { return size; }
    inline void set_size(std::int64_t value) noexcept { size = value; }

    std::uint64_t get_block_offset(size_t block_index) const noexcept;

    void mark_local_available(size_t block_index) noexcept;

    const std::string &get_link_target() const noexcept { return symlink_target; }

    const bfs::path &get_path() noexcept;
    bool is_older(const file_info_t &other) noexcept;

    bool is_incomplete() const noexcept;
    void mark_complete() noexcept;
    void mark_incomplete() noexcept;

    void record_update(const device_t &source) noexcept;
    void after_sync() noexcept;
    file_info_ptr_t link(const device_ptr_t &target) noexcept;

    inline bool is_locked() const noexcept { return locked; }
    void lock() noexcept;
    void unlock() noexcept;

    proto::FileInfo get() const noexcept;

    static const constexpr auto data_length = 1 + uuid_length * 2;


    template <typename Source> void fields_update(const Source &s) noexcept;
  private:
    file_info_t(std::string_view key, const db::FileInfo& data, const folder_info_ptr_t& folder_info_) noexcept;
    file_info_t(const uuid_t& uuid, const proto::FileInfo &info_, const folder_info_ptr_t& folder_info_) noexcept;

    void update_blocks(const proto::FileInfo &remote_info) noexcept;
    void remove_block(block_info_ptr_t &block, block_infos_map_t &cluster_blocks, block_infos_map_t &deleted_blocks,
                      bool zero_indices = true) noexcept;

    char key[data_length];
    std::string name;
    folder_info_t *folder_info;
    proto::FileInfoType type;
    std::int64_t size;
    std::uint32_t permissions;
    std::int64_t modified_s;
    std::uint32_t modified_ns;
    std::uint64_t modified_by;
    bool deleted;
    bool invalid;
    bool no_permissions;
    proto::Vector version;
    std::int64_t sequence;
    std::int32_t block_size;
    std::string symlink_target;
    blocks_t blocks;
    std::optional<bfs::path> path;
    std::string full_name;
    bool locked = false;
    bool incomplete = false;

    friend struct blocks_interator_t;
};

struct file_infos_map_t: public generic_map_t<file_info_ptr_t, 2> {
    file_info_ptr_t by_name(std::string_view name) noexcept;
};

}; // namespace syncspirit::model
