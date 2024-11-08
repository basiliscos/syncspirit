// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include "misc/augmentation.hpp"
#include "misc/map.hpp"
#include "misc/uuid.h"
#include "device_id.h"
#include "folder_data.h"
#include "structs.pb.h"
#include "syncspirit-export.h"
#include <boost/outcome.hpp>

namespace syncspirit::model {

namespace outcome = boost::outcome_v2;

struct pending_folder_t;

using pending_folder_ptr_t = intrusive_ptr_t<pending_folder_t>;

struct SYNCSPIRIT_API pending_folder_t final : augmentable_t<pending_folder_t>, folder_data_t {

    static outcome::result<pending_folder_ptr_t> create(std::string_view key, const db::PendingFolder &data) noexcept;
    static outcome::result<pending_folder_ptr_t> create(const bu::uuid &uuid, const db::PendingFolder &data,
                                                        const device_id_t &device_) noexcept;

    inline const device_id_t &device_id() const noexcept { return device; }
    inline std::uint64_t get_index() const noexcept { return index; }
    inline std::int64_t get_max_sequence() const noexcept { return max_sequence; }
    std::string_view get_key() const noexcept { return std::string_view(key, data_length); }

    void serialize(db::PendingFolder &data) const noexcept;
    std::string serialize() const noexcept;

  private:
    pending_folder_t(std::string_view key, const device_id_t &device_) noexcept;
    pending_folder_t(const bu::uuid &uuid, const device_id_t &device_) noexcept;
    void assign_fields(const db::PendingFolder &data) noexcept;

    device_id_t device;
    std::uint64_t index;
    std::int64_t max_sequence;

    static const constexpr auto data_length = uuid_length + device_id_t::digest_length + 1;
    char key[data_length];
};

struct SYNCSPIRIT_API pending_folder_map_t : generic_map_t<pending_folder_ptr_t, 2> {
    pending_folder_ptr_t by_key(std::string_view id) const noexcept;
    pending_folder_ptr_t by_id(std::string_view id) const noexcept;
};

} // namespace syncspirit::model
