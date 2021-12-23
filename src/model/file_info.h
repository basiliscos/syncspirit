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
    std::string_view get_uuid() const noexcept;
    bool operator==(const file_info_t &other) const noexcept { return get_uuid() == other.get_uuid(); }

    proto::FileInfo as_proto(bool include_blocks = true) const noexcept;
    db::FileInfo as_db(bool include_blocks = true) const noexcept;
    std::string serialize(bool include_blocks = true) const noexcept;

    void update(const proto::FileInfo &remote_info) noexcept;
    bool update(const local_file_t &local_file) noexcept;

    inline folder_info_t *get_folder_info() const noexcept { return folder_info; }
    std::string_view get_name() const noexcept;
    inline const std::string &get_full_name() const noexcept { return full_name; }

    inline std::int64_t get_sequence() const noexcept { return sequence; }
    void set_sequence(std::int64_t value) noexcept;

    // inline blocks_t &aget_blocks() noexcept { return blocks; }
    inline const blocks_t &get_blocks() const noexcept { return blocks; }

    void remove_blocks() noexcept;
    void assign_block(const model::block_info_ptr_t &block, size_t index) noexcept;

    inline bool is_file() const  noexcept { return type == proto::FileInfoType::FILE; }
    inline bool is_dir() const noexcept { return type == proto::FileInfoType::DIRECTORY; }
    inline bool is_link() const noexcept { return type == proto::FileInfoType::SYMLINK; }
    inline bool is_deleted() const noexcept { return deleted; }

    inline std::int64_t get_size() const noexcept { return size; }
    inline void set_size(std::int64_t value) noexcept { size = value; }

    std::int32_t get_block_size() const noexcept { return block_size; }
    std::uint64_t get_block_offset(size_t block_index) const noexcept;

    void mark_local_available(size_t block_index) noexcept;
    bool is_locally_available() noexcept;

    const std::string &get_link_target() const noexcept { return symlink_target; }

    const bfs::path &get_path() const noexcept;
    bool need_download(const file_info_t& other) noexcept;
#if 0
    bool is_older(const file_info_t &other) noexcept;
#endif

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


    outcome::result<void> fields_update(const db::FileInfo&) noexcept;
  private:
    using marks_vector_t = std::vector<bool>;

    template <typename Source> outcome::result<void>  fields_update(const Source &s, size_t block_count) noexcept;
    template<typename T> T as() const noexcept;

    file_info_t(std::string_view key, const folder_info_ptr_t& folder_info_) noexcept;
    file_info_t(const uuid_t& uuid, const folder_info_ptr_t& folder_info_) noexcept;
    outcome::result<void> reserve_blocks(size_t block_count) noexcept;

    void update_blocks(const proto::FileInfo &remote_info) noexcept;
    void remove_block(block_info_ptr_t &block) noexcept;

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
    mutable std::optional<bfs::path> path;
    std::string full_name;
    bool locked = false;
    marks_vector_t marks;
    size_t missing_blocks;

    friend struct blocks_interator_t;
};

struct file_infos_map_t: public generic_map_t<file_info_ptr_t, 2> {
    file_info_ptr_t by_name(std::string_view name) noexcept;
};

}; // namespace syncspirit::model

namespace std {

template <> struct hash<syncspirit::model::file_info_ptr_t> {
    inline size_t operator()(const syncspirit::model::file_info_ptr_t &file) const noexcept {
        return reinterpret_cast<size_t>(file.get());
    }
};
}
