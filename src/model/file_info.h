#pragma once

#include <cstdint>
#include <vector>
#include <optional>
#include <boost/filesystem.hpp>
#include "arc.hpp"
#include "map.hpp"
#include "structs.pb.h"
#include "block_info.h"
#include "storeable.h"

namespace syncspirit::model {

namespace bfs = boost::filesystem;

struct folder_info_t;
struct block_info_t;
struct local_file_t;
struct file_info_t;
struct device_t;

using device_ptr_t = intrusive_ptr_t<device_t>;

struct block_location_t {
    block_info_t *block;
    std::size_t block_index;
    inline operator bool() const noexcept { return (bool)block; }
};

struct blocks_interator_t {
    using blocks_t = std::vector<block_info_ptr_t>;

    blocks_interator_t() noexcept;
    blocks_interator_t(blocks_t &blocks, blocks_t &local_blocks) noexcept;

    template <typename T> blocks_interator_t &operator=(T &other) noexcept {
        blocks = other.blocks;
        local_blocks = other.local_blocks;
        i = other.i;
        return *this;
    }

    inline operator bool() noexcept { return blocks != nullptr; }

    block_location_t next() noexcept;
    void reset() noexcept;

  private:
    void prepare() noexcept;
    blocks_t *blocks;
    blocks_t *local_blocks;
    size_t i;
};

struct file_info_t : arc_base_t<file_info_t>, storeable_t {
    using blocks_t = std::vector<block_info_ptr_t>;
    file_info_t(const db::FileInfo &info_, folder_info_t *folder_info_) noexcept;
    file_info_t(const proto::FileInfo &info_, folder_info_t *folder_info_) noexcept;
    ~file_info_t();

    bool operator==(const file_info_t &other) const noexcept { return other.db_key == db_key; }

    db::FileInfo serialize() noexcept;
    void update(const proto::FileInfo &remote_info) noexcept;
    bool update(const local_file_t &local_file) noexcept;

    inline const std::string &get_db_key() const noexcept { return db_key; }

    inline folder_info_t *get_folder_info() const noexcept { return folder_info; }
    std::string_view get_name() const noexcept;
    inline const std::string &get_full_name() const noexcept { return full_name; }

    inline std::int64_t get_sequence() const noexcept { return sequence; }
    inline blocks_t &get_blocks() noexcept { return blocks; }

    inline bool is_file() noexcept { return type == proto::FileInfoType::FILE; }
    inline bool is_dir() noexcept { return type == proto::FileInfoType::DIRECTORY; }
    inline bool is_link() noexcept { return type == proto::FileInfoType::SYMLINK; }
    inline bool is_deleted() noexcept { return deleted; }

    static std::string generate_db_key(const std::string &name, const folder_info_t &folder) noexcept;

    blocks_interator_t iterate_blocks() noexcept;
    inline std::int64_t get_size() const noexcept { return size; }

    std::uint64_t get_block_offset(size_t block_index) const noexcept;

    void clone_block(file_info_t &source, std::size_t src_block_index, std::size_t dst_block_index) noexcept;
    bool mark_local_available(size_t block_index) noexcept;

    const std::string &get_link_target() const noexcept { return symlink_target; }

    const bfs::path &get_path() noexcept;
    bool is_older(const file_info_t &other) noexcept;

    void record_update(const device_t &source) noexcept;
    void after_sync() noexcept;
    file_info_ptr_t link(const device_ptr_t &target) noexcept;

  private:
    void update_blocks(const proto::FileInfo &remote_info) noexcept;
    void generate_db_key(const std::string &name) noexcept;
    template <typename Source> void fields_update(const Source &s) noexcept;

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
    std::string db_key; /* folder_info db key + name */
    blocks_t blocks;
    blocks_t local_blocks;
    size_t local_blocks_count = 0;
    std::optional<bfs::path> path;
    std::string full_name;
};

inline const std::string &db_key(const file_info_ptr_t &item) noexcept { return item->get_db_key(); }
inline const std::string &natural_key(const file_info_ptr_t &item) noexcept { return item->get_full_name(); }

using file_infos_map_t = generic_map_t<file_info_ptr_t, std::string, std::string>;

}; // namespace syncspirit::model
