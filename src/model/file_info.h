#pragma once

#include <cstdint>
#include <vector>
#include <boost/filesystem.hpp>
#include "arc.hpp"
#include "map.hpp"
#include "structs.pb.h"
#include "block_info.h"
#include "storeable.h"

namespace syncspirit::model {

namespace bfs = boost::filesystem;

struct folder_t;
struct block_info_t;
struct local_file_t;
struct file_info_t;

enum class file_status_t { sync, older, newer };

struct block_location_t {
    block_info_t *block;
    std::size_t block_index;
    inline operator bool() const noexcept { return (bool)block; }
};

struct file_info_t : arc_base_t<file_info_t>, storeable_t {
    using blocks_t = std::vector<block_info_ptr_t>;
    file_info_t(const db::FileInfo &info_, folder_t *folder_) noexcept;
    file_info_t(const proto::FileInfo &info_, folder_t *folder_) noexcept;
    ~file_info_t();

    bool operator==(const file_info_t &other) const noexcept { return other.db_key == db_key; }

    db::FileInfo serialize() noexcept;
    void update(const proto::FileInfo &remote_info) noexcept;
    file_status_t update(const local_file_t &local_file) noexcept;

    inline const std::string &get_db_key() const noexcept { return db_key; }

    inline folder_t *get_folder() const noexcept { return folder; }
    std::string_view get_name() const noexcept;

    inline std::int64_t get_sequence() const noexcept { return sequence; }
    inline blocks_t &get_blocks() noexcept { return blocks; }

    inline void mark_outdated() noexcept { status = file_status_t::older; }
    inline file_status_t get_status() const noexcept { return status; }

    static std::string generate_db_key(const std::string &name, const folder_t &folder) noexcept;

    block_location_t next_block() noexcept;
    inline std::int64_t get_size() const noexcept { return size; }

    std::uint64_t get_block_offset(size_t block_index) const noexcept;

    void clone_block(file_info_t &source, std::size_t src_block_index, std::size_t dst_block_index) noexcept;
    void mark_local_available(size_t block_index) noexcept;

    bfs::path get_path() const noexcept;

  private:
    void update_blocks(const proto::FileInfo &remote_info) noexcept;
    void generate_db_key(const std::string &name) noexcept;
    template <typename Source> void fields_update(const Source &s) noexcept;

    folder_t *folder;
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
    file_status_t status = file_status_t::older;
};

inline const std::string db_key(const file_info_ptr_t &item) noexcept { return item->get_db_key(); }

using file_infos_map_t = generic_map_t<file_info_ptr_t, void, std::string>;

}; // namespace syncspirit::model
