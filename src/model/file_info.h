#pragma once

#include <cstdint>
#include <vector>
#include <optional>
#include <boost/filesystem.hpp>
#include <boost/outcome.hpp>
#include "misc/arc.hpp"
#include "misc/map.hpp"
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

struct file_info_t final : arc_base_t<file_info_t> {

    enum flags_t {
        f_deleted = 1 << 0,
        f_invalid = 1 << 1,
        f_no_permissions = 1 << 2,
        f_locked = 1 << 3,
        f_local_locked = 1 << 4,
    };

    using blocks_t = std::vector<block_info_ptr_t>;

    static outcome::result<file_info_ptr_t> create(std::string_view key, const db::FileInfo &data,
                                                   const folder_info_ptr_t &folder_info_) noexcept;
    static outcome::result<file_info_ptr_t> create(const uuid_t &uuid, const proto::FileInfo &info_,
                                                   const folder_info_ptr_t &folder_info_) noexcept;
    static std::string create_key(const uuid_t &uuid, const folder_info_ptr_t &folder_info_) noexcept;

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
    inline const proto::Vector &get_version() const noexcept { return version; };

    inline std::int64_t get_sequence() const noexcept { return sequence; }
    void set_sequence(std::int64_t value) noexcept;

    inline const blocks_t &get_blocks() const noexcept { return blocks; }

    void remove_blocks() noexcept;
    void assign_block(const model::block_info_ptr_t &block, size_t index) noexcept;

    inline bool is_file() const noexcept { return type == proto::FileInfoType::FILE; }
    inline bool is_dir() const noexcept { return type == proto::FileInfoType::DIRECTORY; }
    inline bool is_link() const noexcept { return type == proto::FileInfoType::SYMLINK; }
    inline bool is_deleted() const noexcept { return flags & f_deleted; }

    inline std::int64_t get_size() const noexcept { return size; }
    inline void set_size(std::int64_t value) noexcept { size = value; }

    std::int32_t get_block_size() const noexcept { return block_size; }
    std::uint64_t get_block_offset(size_t block_index) const noexcept;

    void mark_local_available(size_t block_index) noexcept;
    bool is_locally_available(size_t block_index) const noexcept;
    bool is_locally_available() const noexcept;

    const std::string &get_link_target() const noexcept { return symlink_target; }

    const bfs::path &get_path() const noexcept;
    bool need_download(const file_info_t &other) noexcept;

    inline std::int64_t get_modified_s() const noexcept { return modified_s; }

    bool is_incomplete() const noexcept;
    void mark_complete() noexcept;
    void mark_incomplete() noexcept;

    file_info_ptr_t local_file() noexcept;

    bool is_locked() const noexcept;
    void lock() noexcept;
    void unlock() noexcept;

    bool is_locally_locked() const noexcept;
    void locally_lock() noexcept;
    void locally_unlock() noexcept;

    proto::FileInfo get() const noexcept;
    file_info_ptr_t get_source() const noexcept;
    void set_source(const file_info_ptr_t &peer_file) noexcept;

    static const constexpr auto data_length = 1 + uuid_length * 2;

    outcome::result<void> fields_update(const db::FileInfo &) noexcept;

  private:
    using marks_vector_t = std::vector<bool>;

    template <typename Source> outcome::result<void> fields_update(const Source &s, size_t block_count) noexcept;
    template <typename T> T as() const noexcept;

    file_info_t(std::string_view key, const folder_info_ptr_t &folder_info_) noexcept;
    file_info_t(const uuid_t &uuid, const folder_info_ptr_t &folder_info_) noexcept;
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

    int flags = 0;
    proto::Vector version;
    proto::Vector source_version;
    std::int64_t sequence;
    std::int32_t block_size;
    std::string symlink_target;
    blocks_t blocks;
    mutable std::optional<bfs::path> path;
    std::string full_name;
    marks_vector_t marks;
    size_t missing_blocks;
    std::string source_device;

    friend struct blocks_interator_t;
};

struct file_infos_map_t : public generic_map_t<file_info_ptr_t, 2> {
    file_info_ptr_t by_name(std::string_view name) noexcept;
};

}; // namespace syncspirit::model

namespace std {

template <> struct hash<syncspirit::model::file_info_ptr_t> {
    inline size_t operator()(const syncspirit::model::file_info_ptr_t &file) const noexcept {
        return reinterpret_cast<size_t>(file.get());
    }
};
} // namespace std
