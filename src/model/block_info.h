#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "arc.hpp"
#include "map.hpp"
#include "file_block.h"
#include "structs.pb.h"
#include "storeable.h"

namespace syncspirit::model {

struct file_info_t;
using file_info_ptr_t = intrusive_ptr_t<file_info_t>;
struct file_block_t;

struct block_info_t : arc_base_t<block_info_t>, storeable_t {
    using removed_incides_t = std::vector<size_t>;

    block_info_t(const db::BlockInfo &db_block, std::uint64_t db_key_ = 0) noexcept;
    block_info_t(const proto::BlockInfo &block) noexcept;

    inline std::uint64_t get_db_key() const noexcept { return db_key; }
    inline void set_db_key(std::uint64_t value) noexcept { db_key = value; }

    inline const std::string &get_hash() const noexcept { return hash; }
    inline std::uint32_t get_weak_hash() const noexcept { return weak_hash; }
    inline std::uint32_t get_size() const noexcept { return size; }
    inline size_t usages() const noexcept { return file_blocks.size(); }

    db::BlockInfo serialize() noexcept;
    void link(file_info_t *file_info, size_t block_index) noexcept;
    removed_incides_t unlink(file_info_t *file_info, bool deletion = false) noexcept;

    void mark_local_available(file_info_t *file_info) noexcept;
    file_block_t local_file() noexcept;

    inline bool operator==(const block_info_t &right) const noexcept { return hash == right.hash; }
    inline bool operator!=(const block_info_t &right) const noexcept { return !(hash == right.hash); }

  private:
    using file_blocks_t = std::vector<file_block_t>;

    std::string hash;
    std::uint32_t weak_hash;
    std::int32_t size;
    std::uint64_t db_key;
    file_blocks_t file_blocks;
};

using block_info_ptr_t = intrusive_ptr_t<block_info_t>;

inline const std::string &natural_key(const block_info_ptr_t &item) noexcept { return item->get_hash(); }
inline std::uint64_t db_key(const block_info_ptr_t &item) noexcept { return item->get_db_key(); }

using block_infos_map_t = generic_map_t<block_info_ptr_t, std::string, std::uint64_t>;

} // namespace syncspirit::model
