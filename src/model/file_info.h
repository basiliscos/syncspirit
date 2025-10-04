// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include <cstdint>
#include <unordered_set>
#include <filesystem>
#include <boost/outcome.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include "utils/compact_vector.hpp"
#include "misc/augmentation.h"
#include "misc/path.h"
#include "misc/map.hpp"
#include "misc/uuid.h"
#include "block_info.h"
#include "version.h"
#include "proto/proto-fwd.hpp"
#include "syncspirit-export.h"

namespace syncspirit::model {

namespace bfs = std::filesystem;
namespace outcome = boost::outcome_v2;

struct folder_info_t;
using folder_info_ptr_t = intrusive_ptr_t<folder_info_t>;

struct local_file_t;
struct blocks_iterator_t;

struct file_info_t;
using file_info_ptr_t = intrusive_ptr_t<file_info_t>;

struct path_cache_t;
struct path_guard_t;

struct SYNCSPIRIT_API file_info_t {

    // clang-format off
    enum flags_t: std::uint16_t {
        f_type_file        = 1 << 0,
        f_type_dir         = 1 << 1,
        f_type_link        = 1 << 2,
        f_deleted          = 1 << 3,
        f_invalid          = 1 << 4,
        f_no_permissions   = 1 << 5,
        f_synchronizing    = 1 << 6,
        f_unreachable      = 1 << 7,
        f_local            = 1 << 8,
        f_available        = 1 << 9,
    };
    // clang-format on

    static constexpr std::uintptr_t LOCAL_MASK = 1 << 0;
    static constexpr std::uintptr_t PTR_MASK = ~LOCAL_MASK;

    using blocks_t = utils::compact_vector_t<block_info_t *>;

    struct decomposed_key_t {
        utils::bytes_view_t folder_info_id;
        utils::bytes_view_t file_id;
    };

    struct guard_t {
        using path_guard_ptr_t = std::unique_ptr<path_guard_t>;
        guard_t() noexcept = default;
        guard_t(file_info_t &file, const folder_info_t *folder_info) noexcept;
        guard_t(const guard_t &) = delete;
        guard_t(guard_t &&) = default;
        ~guard_t();

        guard_t &operator=(guard_t &&) noexcept = default;

        file_info_ptr_t file;
        path_guard_ptr_t path_guard;
        const folder_info_t *folder_info;
    };

    struct SYNCSPIRIT_API blocks_iterator_t {
        using indexed_block_t = std::pair<const block_info_t *, std::uint32_t>;
        blocks_iterator_t() = default;
        blocks_iterator_t(const file_info_t *file, std::uint32_t start_index) noexcept;

        blocks_iterator_t(blocks_iterator_t &&) = default;
        blocks_iterator_t(const blocks_iterator_t &) = delete;

        blocks_iterator_t &operator=(blocks_iterator_t &&) = default;

        const block_info_t *next() noexcept;
        indexed_block_t current() const noexcept;
        std::uint32_t get_total() const noexcept;

      private:
        const file_info_t *file = nullptr;
        std::uint32_t next_index = 0;
    };

    static outcome::result<file_info_ptr_t> create(utils::bytes_view_t key, const db::FileInfo &data,
                                                   const folder_info_ptr_t &folder_info_) noexcept;
    static outcome::result<file_info_ptr_t> create(const bu::uuid &uuid, const proto::FileInfo &info_,
                                                   const folder_info_ptr_t &folder_info_) noexcept;
    static utils::bytes_t create_key(const bu::uuid &uuid, const folder_info_ptr_t &folder_info_) noexcept;

    static decomposed_key_t decompose_key(utils::bytes_view_t key);

    static inline std::uint16_t as_flags(proto::FileInfoType type) noexcept {
        return type == proto::FileInfoType::DIRECTORY ? f_type_dir
               : type == proto::FileInfoType::SYMLINK ? f_type_link
                                                      : f_type_file;
    }

    ~file_info_t();

    inline bool operator==(const file_info_t &other) const noexcept { return get_uuid() == other.get_uuid(); }

    utils::bytes_view_t get_uuid() const noexcept;
    utils::bytes_view_t get_full_id() const noexcept;
    utils::bytes_view_t get_folder_uuid() const noexcept;

    proto::FileInfo as_proto(bool include_blocks = true) const noexcept;
    db::FileInfo as_db(bool include_blocks = true) const noexcept;
    utils::bytes_t serialize(bool include_blocks = true) const noexcept;

    void update(const file_info_t &updated) noexcept;

    const path_ptr_t &get_name() const noexcept;
    inline version_t &get_version() noexcept { return version; }
    inline const version_t &get_version() const noexcept { return version; }

    inline std::int64_t get_sequence() const noexcept { return sequence; }
    void set_sequence(std::int64_t value) noexcept;

    inline blocks_iterator_t iterate_blocks(std::uint32_t start_index = 0) const noexcept {
        return blocks_iterator_t(this, start_index);
    }

    void remove_blocks() noexcept;
    void assign_block(model::block_info_t *block, size_t index) noexcept;

    inline std::uint16_t get_type() const noexcept { return flags & 0b111; }
    inline bool is_file() const noexcept { return flags & f_type_file; }
    inline bool is_dir() const noexcept { return flags & f_type_dir; }
    inline bool is_link() const noexcept { return flags & f_type_link; }
    inline bool is_deleted() const noexcept { return flags & f_deleted; }
    inline bool is_invalid() const noexcept { return flags & f_invalid; }
    inline bool is_unreachable() const noexcept { return flags & f_unreachable; }
    inline bool is_local() const noexcept { return flags & f_local; }

    std::int64_t get_size() const noexcept;

    std::int32_t get_block_size() const noexcept {
        auto has_blocks = (flags & f_type_file && !content.file.blocks.empty());
        if (has_blocks) {
            auto &blocks = content.file.blocks;
            auto ptr = reinterpret_cast<std::uintptr_t>(blocks.front());
            auto block = reinterpret_cast<const block_info_t *>(ptr & PTR_MASK);
            return block->get_size();
        }
        return 0;
    }
    std::uint64_t get_block_offset(size_t block_index) const noexcept;

    void mark_unreachable(bool value) noexcept;
    void mark_local_available(size_t block_index) noexcept;
    void mark_local(bool available) noexcept;
    bool is_locally_available(size_t block_index) const noexcept;
    bool is_locally_available() const noexcept;

    std::string_view get_link_target() const noexcept {
        if (flags & f_type_link) {
            auto &container = content.non_file.symlink_target;
            return {container.data(), container.size()};
        }
        return {};
    }

    const bfs::path get_path(const folder_info_t &folder_info) const noexcept;

    inline std::int64_t get_modified_s() const noexcept { return modified_s; }
    inline std::int32_t get_modified_ns() const noexcept { return modified_ns; }
    inline std::uint64_t get_modified_by() const noexcept { return modified_by; }

    bool is_synchronizing() const noexcept;
    void synchronizing_lock() noexcept;
    void synchronizing_unlock() noexcept;

    proto::FileInfo get() const noexcept;
    bool identical_to(const proto::FileInfo &file) const noexcept;

    static const constexpr auto data_length = uuid_length * 2;

    outcome::result<void> fields_update(const db::FileInfo &, model::path_cache_t &) noexcept;
    outcome::result<void> fields_update(const proto::FileInfo &, model::path_cache_t &) noexcept;

    proto::Index generate() noexcept;
    std::size_t expected_meta_size() const noexcept;

    std::uint32_t get_permissions() const noexcept;
    bool has_no_permissions() const noexcept;

    guard_t guard(const model::folder_info_t &folder_info) noexcept;

    std::string make_conflicting_name() const noexcept;

    inline void refcouner_inc() const noexcept { ++counter; }
    inline std::uint32_t refcouner_dec() const noexcept { return --counter; }
    inline std::uint32_t use_count() const noexcept { return counter; }

    void set_augmentation(augmentation_t &value) noexcept;
    void set_augmentation(augmentation_ptr_t value) noexcept;
    augmentation_ptr_t &get_augmentation() noexcept;
    void notify_update() noexcept;

    template <typename T> auto &access() noexcept;
    template <typename T> auto &access() const noexcept;

    void recheck(const folder_info_t &folder_info) noexcept;

  private:
    using string_t = utils::compact_vector_t<char>;
    struct size_full_t {
        ~size_full_t();
        blocks_t blocks;
    };

    struct size_less_t {
        ~size_less_t();
        string_t symlink_target;
    };

    union content_t {
        content_t();
        ~content_t();
        size_full_t file;
        size_less_t non_file;
    };

    file_info_t(utils::bytes_view_t key, const folder_info_ptr_t &folder_info_) noexcept;
    file_info_t(const bu::uuid &uuid, const folder_info_ptr_t &folder_info_) noexcept;
    void reserve_blocks(size_t block_count) noexcept;

    void update_blocks(const proto::FileInfo &remote_info) noexcept;
    void remove_block(block_info_t *block) noexcept;

    unsigned char key[data_length];
    augmentation_ptr_t extension;
    path_ptr_t name;
    std::int64_t modified_s;
    std::uint64_t modified_by;
    std::int64_t sequence;
    std::uint32_t permissions;
    std::uint32_t modified_ns;

    content_t content;
    version_t version;
    std::uint16_t flags = 0;
    mutable std::uint16_t counter = 0;

    friend struct blocks_iterator_t;
    friend struct model::blocks_iterator_t;
};

inline void intrusive_ptr_add_ref(const file_info_t *ptr) noexcept { ptr->refcouner_inc(); }

inline void intrusive_ptr_release(const file_info_t *ptr) noexcept {
    if (ptr->refcouner_dec() == 0) {
        delete ptr;
    }
}

namespace details {

template <> struct indexed_item_t<file_info_ptr_t, 3> {
    static constexpr size_t size = 3;
    using storage_t = std::tuple<utils::bytes_t, utils::bytes_t, std::int64_t>;
    using item_t = file_info_ptr_t;
    item_t item;
    mutable storage_t keys;

    indexed_item_t(const item_t &item_, const storage_t &keys_) noexcept : item{item_}, keys{keys_} {}

    template <size_t I> auto &get() noexcept { return std::get<I>(keys); }
};

using indexed_file_item_t = indexed_item_t<file_info_ptr_t, 3>;

template <> struct indexed_by<2, indexed_file_item_t> {
    using K = indexed_file_item_t;
    using type = mi::ordered_unique<mi::global_fun<const K &, single_key_t<K, 2>, &get_key<2, K>>>;
};

} // namespace details

namespace file_details {

namespace mi = boost::multi_index;

inline utils::bytes_view_t get_uuid(const model::file_info_ptr_t &file) noexcept { return file->get_uuid(); }
inline std::string_view get_name(const model::file_info_ptr_t &file) noexcept {
    return file->get_name()->get_full_name();
}
inline std::int64_t get_sequence(const model::file_info_ptr_t &file) noexcept { return file->get_sequence(); }

// clang-format off
using file_map_base_t = mi::multi_index_container<
    model::file_info_ptr_t,
    mi::indexed_by<
        mi::hashed_unique<mi::global_fun<const model::file_info_ptr_t &, utils::bytes_view_t, &get_uuid>>,
        mi::hashed_unique<mi::global_fun<const model::file_info_ptr_t &, std::string_view, &get_name>>,
        mi::ordered_unique<mi::global_fun<const model::file_info_ptr_t &, std::int64_t, &get_sequence>>
    >
>;
// clang-format on

} // namespace file_details

struct files_map_t : file_details::file_map_base_t {
    using parent_t = file_details::file_map_base_t;
    files_map_t() = default;
    files_map_t(const files_map_t &) = delete;

    bool put(const model::file_info_ptr_t &item, bool replace = true) noexcept {
        auto [it, inserted] = parent_t::emplace(item);
        if (!inserted && replace) {
            return parent_t::replace(it, item);
        }
        return inserted;
    }
};

struct SYNCSPIRIT_API file_infos_map_t : private file_details::file_map_base_t {
    using parent_t = file_details::file_map_base_t;
    using seq_projection_t = std::remove_cv_t<decltype(std::declval<parent_t>().template get<2>())>;
    using seq_iterator_t = decltype(std::declval<seq_projection_t>().begin());
    using range_t = std::pair<seq_iterator_t, seq_iterator_t>;

    using parent_t::begin;
    using parent_t::clear;
    using parent_t::end;
    using parent_t::size;

    bool put(const model::file_info_ptr_t &item, bool replace = true) noexcept;
    void remove(const model::file_info_ptr_t &item) noexcept;
    file_info_ptr_t by_uuid(utils::bytes_view_t) const noexcept;
    file_info_ptr_t by_name(std::string_view name) const noexcept;
    file_info_ptr_t by_sequence(std::int64_t sequence) const noexcept;
    seq_projection_t &sequence_projection() noexcept;
    range_t range(std::int64_t lower, std::int64_t upper) noexcept;
};

using file_infos_set_t = std::unordered_set<file_info_ptr_t>;

}; // namespace syncspirit::model

namespace std {

template <> struct hash<syncspirit::model::file_info_ptr_t> {
    inline size_t operator()(const syncspirit::model::file_info_ptr_t &file) const noexcept {
        return reinterpret_cast<size_t>(file.get());
    }
};

template <> struct hash<syncspirit::model::file_info_t::guard_t> {
    inline size_t operator()(const syncspirit::model::file_info_t::guard_t &guard) const noexcept {
        return reinterpret_cast<size_t>(guard.file.get());
    }
};
} // namespace std
