// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2026 Ivan Baidakou

#pragma once

#include <cstdint>
#include "presence.h"
#include "device.h"
#include "file_info.h"
#include "proto/proto-fwd.hpp"
#include "syncspirit-export.h"
#include "utils/bytes.h"
#include "misc/orphaned_files.h"
#include <boost/outcome.hpp>

namespace syncspirit::model {

namespace outcome = boost::outcome_v2;

struct folder_t;
using folder_ptr_t = intrusive_ptr_t<folder_t>;

struct folder_info_t;
using folder_info_ptr_t = intrusive_ptr_t<folder_info_t>;

struct SYNCSPIRIT_API folder_info_t final : presence_t {

    struct decomposed_key_t {
        static constexpr size_t folder_data_length = uuid_length + 1;

        decomposed_key_t(utils::bytes_view_t reduced_key, utils::bytes_view_t folder_uuid,
                         utils::bytes_view_t folder_info_id);
        decomposed_key_t(const decomposed_key_t &) = default;
        decomposed_key_t(decomposed_key_t &&) = default;

        inline utils::bytes_view_t const device_key() {
            return utils::bytes_view_t(device_key_raw, device_id_t::data_length);
        }
        inline utils::bytes_view_t const folder_key() {
            return utils::bytes_view_t(folder_key_raw, folder_data_length);
        }

        utils::bytes_view_t folder_info_id;
        unsigned char device_key_raw[device_id_t::data_length];
        unsigned char folder_key_raw[folder_data_length];
    };
    using location_t = std::pair<file_info_t *, presence_t *>;

    static decomposed_key_t decompose_key(utils::bytes_view_t key);

    static outcome::result<folder_info_ptr_t> create(utils::bytes_view_t key, const db::FolderInfo &data,
                                                     const device_ptr_t &device_, const folder_ptr_t &folder_) noexcept;
    static outcome::result<folder_info_ptr_t> create(const bu::uuid &uuid, const db::FolderInfo &data,
                                                     const device_ptr_t &device_, const folder_ptr_t &folder_) noexcept;
    utils::bytes_view_t get_key() const noexcept;
    utils::bytes_view_t get_uuid() const noexcept;

    bool operator==(const folder_info_t &other) const noexcept;
    bool operator!=(const folder_info_t &other) const noexcept { return !(*this == other); }

    void add_relaxed(file_info_t* file_info) noexcept;
    bool add_strict(file_info_t* file_info, bool already_added) noexcept;
    void update_max_sequnece(const file_info_t& file_info) noexcept;

    void serialize(db::FolderInfo &storage) const noexcept;
    utils::bytes_t serialize() const noexcept;

    inline std::uint64_t get_index() const noexcept { return index; }
    void set_index(std::uint64_t value) noexcept;

    inline device_t *get_device() const noexcept { return device; }
    folder_t *get_folder() const noexcept;
    inline std::int64_t get_max_sequence() const noexcept { return max_sequence; }

    location_t get_by_path(const utils::path_base_t& path) const noexcept;
    void clear() noexcept;

    bool is_introduced_by(const model::device_id_t &device) const noexcept;
    utils::bytes_view_t get_introducer_device_key() const noexcept;

    orphaned_files_t& get_orphaned_files() noexcept { return orphaned_files; }

  private:
    folder_info_t(utils::bytes_view_t key, const device_ptr_t &device_, const folder_ptr_t &folder_) noexcept;
    folder_info_t(const bu::uuid &uuid, const device_ptr_t &device_, const folder_ptr_t &folder_) noexcept;
    void assign_fields(const db::FolderInfo &data) noexcept;
    void add(file_info_t* file_info) noexcept;

    static const constexpr auto data_length = uuid_length * 2 + device_id_t::digest_length + 1;

    unsigned char key[data_length];

    std::uint64_t index;
    std::int64_t max_sequence;
    utils::bytes_t introducer_device_key;
    orphaned_files_t orphaned_files;
};

using folder_info_ptr_t = intrusive_ptr_t<folder_info_t>;

using uuid_folder_infos_map_t = std::unordered_map<utils::bytes_view_t, folder_info_t *>;

}; // namespace syncspirit::model
