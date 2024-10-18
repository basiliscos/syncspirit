// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include <cstdint>
#include <optional>
#include "misc/augmentation.hpp"
#include "device.h"
#include "file_info.h"
#include "structs.pb.h"
#include "syncspirit-export.h"
#include <boost/outcome.hpp>

namespace syncspirit::model {

namespace outcome = boost::outcome_v2;

struct folder_t;
using folder_ptr_t = intrusive_ptr_t<folder_t>;

struct folder_info_t;
using folder_info_ptr_t = intrusive_ptr_t<folder_info_t>;

struct SYNCSPIRIT_API folder_info_t final : augmentable_t<folder_info_t> {

    struct decomposed_key_t {
        static constexpr size_t folder_data_length = uuid_length + 1;

        decomposed_key_t(std::string_view reduced_key, std::string_view folder_uuid, std::string_view folder_info_id);
        decomposed_key_t(const decomposed_key_t &) = default;
        decomposed_key_t(decomposed_key_t &&) = default;

        inline std::string_view const device_key() {
            return std::string_view(device_key_raw, device_id_t::data_length);
        }
        inline std::string_view const folder_key() { return std::string_view(folder_key_raw, folder_data_length); }

        std::string_view folder_info_id;
        char device_key_raw[device_id_t::data_length];
        char folder_key_raw[folder_data_length];
    };

    static decomposed_key_t decompose_key(std::string_view key);

    static outcome::result<folder_info_ptr_t> create(std::string_view key, const db::FolderInfo &data,
                                                     const device_ptr_t &device_, const folder_ptr_t &folder_) noexcept;
    static outcome::result<folder_info_ptr_t> create(const uuid_t &uuid, const db::FolderInfo &data,
                                                     const device_ptr_t &device_, const folder_ptr_t &folder_) noexcept;
    std::string_view get_key() const noexcept;
    std::string_view get_uuid() const noexcept;

    bool operator==(const folder_info_t &other) const noexcept;
    bool operator!=(const folder_info_t &other) const noexcept { return !(*this == other); }

    void add(const file_info_ptr_t &file_info, bool inc_max_sequence) noexcept;
    void serialize(db::FolderInfo &storage) const noexcept;
    std::string serialize() const noexcept;

    inline std::uint64_t get_index() const noexcept { return index; }
    void set_index(std::uint64_t value) noexcept;

    inline device_t *get_device() const noexcept { return device; }
    inline folder_t *get_folder() const noexcept { return folder; }
    inline std::int64_t get_max_sequence() const noexcept { return max_sequence; }
    void set_max_sequence(std::int64_t value) noexcept;
    inline file_infos_map_t &get_file_infos() noexcept { return file_infos; }
    inline const file_infos_map_t &get_file_infos() const noexcept { return file_infos; }
    std::optional<proto::Index> generate() noexcept;

  private:
    folder_info_t(std::string_view key, const device_ptr_t &device_, const folder_ptr_t &folder_) noexcept;
    folder_info_t(const uuid_t &uuid, const device_ptr_t &device_, const folder_ptr_t &folder_) noexcept;
    void assign_fields(const db::FolderInfo &data) noexcept;

    static const constexpr auto data_length = uuid_length * 2 + device_id_t::digest_length + 1;

    char key[data_length];

    std::uint64_t index;
    std::int64_t max_sequence;
    device_t *device;
    folder_t *folder;
    file_infos_map_t file_infos;
    bool actualized;
};

using folder_info_ptr_t = intrusive_ptr_t<folder_info_t>;

struct SYNCSPIRIT_API folder_infos_map_t : public generic_map_t<folder_info_ptr_t, 3> {
    folder_info_ptr_t by_device(const device_t &device) const noexcept;
    folder_info_ptr_t by_device_id(std::string_view device_id) const noexcept;
    folder_info_ptr_t by_device_key(std::string_view device_id) const noexcept;
};

}; // namespace syncspirit::model
