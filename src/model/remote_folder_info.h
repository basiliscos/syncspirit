// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2023 Ivan Baidakou

#pragma once

#include <cstdint>
#include "syncspirit-export.h"
#include "bep.pb.h"
#include <boost/outcome.hpp>
#include "misc/arc.hpp"
#include "misc/map.hpp"

namespace syncspirit::model {

namespace outcome = boost::outcome_v2;

struct folder_t;
using folder_ptr_t = intrusive_ptr_t<folder_t>;

struct device_t;
using device_ptr_t = intrusive_ptr_t<device_t>;

struct remote_folder_info_t;
using remote_folder_info_t_ptr_t = intrusive_ptr_t<remote_folder_info_t>;

struct folder_info_t;
using folder_info_ptr_t = intrusive_ptr_t<folder_info_t>;

struct SYNCSPIRIT_API remote_folder_info_t final : arc_base_t<remote_folder_info_t> {

    static outcome::result<remote_folder_info_t_ptr_t> create(const proto::Device &folder, const device_ptr_t &device_,
                                                              const folder_ptr_t &folder_) noexcept;

    std::string_view get_key() const noexcept;

    inline std::uint64_t get_index() const noexcept { return index_id; }
    inline std::int64_t get_max_sequence() const noexcept { return max_sequence; }
    bool needs_update() const noexcept;

    folder_info_ptr_t get_local() const noexcept;

  private:
    remote_folder_info_t(const proto::Device &folder, const device_ptr_t &device_,
                         const folder_ptr_t &folder_) noexcept;

    std::uint64_t index_id;
    std::int64_t max_sequence;
    device_t *device;
    folder_t *folder;
};

struct SYNCSPIRIT_API remote_folder_infos_map_t : public generic_map_t<remote_folder_info_t_ptr_t, 1> {
    remote_folder_info_t_ptr_t by_folder(const folder_t &folder) const noexcept;
};

} // namespace syncspirit::model
