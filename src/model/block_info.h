#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "arc.hpp"
#include "map.hpp"
#include "file_block.h"
#include "storeable.h"
#include "bep.pb.h"

namespace syncspirit::model {

struct file_info_t;
using file_info_ptr_t = intrusive_ptr_t<file_info_t>;
struct file_block_t;

struct block_info_t : arc_base_t<block_info_t>, storeable_t {
    using removed_incides_t = std::vector<size_t>;
    static const constexpr size_t digest_length = 32;
    static const constexpr size_t data_length = digest_length + 1;

    block_info_t(std::string_view key, std::string_view data) noexcept;
    block_info_t(const proto::BlockInfo &block) noexcept;

    inline std::string_view get_hash() const noexcept { return std::string_view(hash + 1, digest_length); }
    inline std::string_view get_key() const noexcept { return std::string_view(hash, data_length); }
    inline std::uint32_t get_weak_hash() const noexcept { return weak_hash; }
    inline std::uint32_t get_size() const noexcept { return size; }
    inline size_t usages() const noexcept { return file_blocks.size(); }

    std::string serialize() noexcept;
    void link(file_info_t *file_info, size_t block_index) noexcept;
    removed_incides_t unlink(file_info_t *file_info, bool deletion = false) noexcept;

    void mark_local_available(file_info_t *file_info) noexcept;
    file_block_t local_file() noexcept;

    inline bool operator==(const block_info_t &right) const noexcept { return hash == right.hash; }
    inline bool operator!=(const block_info_t &right) const noexcept { return !(hash == right.hash); }

  private:
    using file_blocks_t = std::vector<file_block_t>;

    char hash[data_length];
    std::uint32_t weak_hash;
    std::int32_t size;
    file_blocks_t file_blocks;
};

using block_info_ptr_t = intrusive_ptr_t<block_info_t>;

struct block_infos_map_t: generic_map_t<block_info_ptr_t, 2> {
    block_info_ptr_t byHash(std::string_view hash) noexcept;
};

} // namespace syncspirit::model
