#pragma once

#include <cstdint>
#include <vector>
#include "arc.hpp"
#include "map.hpp"
#include "structs.pb.h"
#include "block_info.h"
#include "storeable.h"

namespace syncspirit::model {

struct folder_t;
struct block_info_t;

struct file_info_t : arc_base_t<file_info_t>, storeable_t {
    using blocks_t = std::vector<block_info_ptr_t>;
    file_info_t(const db::FileInfo &info_, folder_t *folder_) noexcept;
    file_info_t(const proto::FileInfo &info_, folder_t *folder_) noexcept;
    ~file_info_t();

    bool operator==(const file_info_t &other) const noexcept { return other.db_key == db_key; }

    db::FileInfo serialize() noexcept;
    void update(const proto::FileInfo &remote_info) noexcept;

    inline const std::string &get_db_key() const noexcept { return db_key; }

    inline folder_t *get_folder() const noexcept { return folder; }
    std::string_view get_name() const noexcept;

    inline std::int64_t get_sequence() const noexcept { return sequence; }
    inline blocks_t &get_blocks() noexcept { return blocks; }

    static std::string generate_db_key(const std::string &name, const folder_t &folder) noexcept;

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
};

using file_info_ptr_t = intrusive_ptr_t<file_info_t>;

inline const std::string db_key(const file_info_ptr_t &item) noexcept { return item->get_db_key(); }

using file_infos_map_t = generic_map_t<file_info_ptr_t, void, std::string>;

}; // namespace syncspirit::model