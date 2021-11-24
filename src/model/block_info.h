#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "misc/arc.hpp"
#include "misc/map.hpp"
#include "misc/file_block.h"
#include "misc/storeable.h"
#include "bep.pb.h"
#include "structs.pb.h"
#include <boost/outcome.hpp>

namespace syncspirit::model {

namespace outcome = boost::outcome_v2;

struct file_info_t;
using file_info_ptr_t = intrusive_ptr_t<file_info_t>;
struct file_block_t;
struct block_info_t;
using block_info_ptr_t = intrusive_ptr_t<block_info_t>;


struct block_info_t final : arc_base_t<block_info_t>, storeable_t {
    using removed_incides_t = std::vector<size_t>;
    using file_blocks_t = std::vector<file_block_t>;
    static const constexpr size_t digest_length = 32;
    static const constexpr size_t data_length = digest_length + 1;

    static outcome::result<block_info_ptr_t> create(std::string_view key, const db::BlockInfo& data) noexcept;
    static outcome::result<block_info_ptr_t> create(const proto::BlockInfo &block) noexcept;

    inline std::string_view get_hash() const noexcept { return std::string_view(hash + 1, digest_length); }
    inline std::string_view get_key() const noexcept { return std::string_view(hash, data_length); }
    inline std::uint32_t get_weak_hash() const noexcept { return weak_hash; }
    inline std::uint32_t get_size() const noexcept { return size; }
    inline size_t usages() const noexcept { return file_blocks.size(); }
    inline file_blocks_t& get_file_blocks() { return file_blocks; }

    proto::BlockInfo as_bep(size_t offset) const noexcept;
    std::string serialize() const noexcept;

    void link(file_info_t *file_info, size_t block_index) noexcept;
    removed_incides_t unlink(file_info_t *file_info, bool deletion = false) noexcept;

    void mark_local_available(file_info_t *file_info) noexcept;
    file_block_t local_file() noexcept;

    inline bool operator==(const block_info_t &right) const noexcept { return get_hash() == right.get_hash(); }
    inline bool operator!=(const block_info_t &right) const noexcept { return !(get_hash() == right.get_hash()); }

  private:
    template<typename T> void assign(const T& item) noexcept;
    block_info_t(std::string_view key) noexcept;
    block_info_t(const proto::BlockInfo &block) noexcept;

    char hash[data_length];
    std::uint32_t weak_hash;
    std::int32_t size;
    file_blocks_t file_blocks;
};

using block_infos_map_t = generic_map_t<block_info_ptr_t, 1>;

} // namespace syncspirit::model
