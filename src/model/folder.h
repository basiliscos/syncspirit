// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#pragma once

#include <cstdint>
#include <optional>
#include <boost/filesystem.hpp>
#include <boost/outcome.hpp>
#include "misc/augmentation.hpp"
#include "device.h"
#include "folder_info.h"
#include "misc/local_file.h"
#include "misc/uuid.h"
#include "folder_data.h"
#include "syncspirit-export.h"
#include "bep.pb.h"

namespace syncspirit::model {

namespace bfs = boost::filesystem;
namespace outcome = boost::outcome_v2;

struct cluster_t;
using cluster_ptr_t = intrusive_ptr_t<cluster_t>;

struct folder_t;

using folder_ptr_t = intrusive_ptr_t<folder_t>;

struct SYNCSPIRIT_API folder_t final : augmentable_t<folder_t>, folder_data_t {

    static outcome::result<folder_ptr_t> create(std::string_view key, const db::Folder &folder) noexcept;
    static outcome::result<folder_ptr_t> create(const uuid_t &uuid, const db::Folder &folder) noexcept;

    void assign_cluster(const cluster_ptr_t &cluster) noexcept;
    void add(const folder_info_ptr_t &folder_info) noexcept;
    std::string serialize() noexcept;
    using folder_data_t::serialize;

    bool operator==(const folder_t &other) const noexcept { return get_id() == other.get_id(); }
    bool operator!=(const folder_t &other) const noexcept { return !(*this == other); }

    folder_info_ptr_t is_shared_with(const device_t &device) const noexcept;

    std::string_view get_key() const noexcept { return std::string_view(key, data_length); }
    inline auto &get_folder_infos() noexcept { return folder_infos; }
    inline cluster_t *&get_cluster() noexcept { return cluster; }

    using folder_data_t::get_path;
    using folder_data_t::set_path;
    void update(local_file_map_t &local_files) noexcept;
    std::optional<proto::Folder> generate(const model::device_t &device) const noexcept;

    template <typename T> auto &access() noexcept;
    template <typename T> auto &access() const noexcept;

    static const constexpr size_t data_length = uuid_length + 1;

  private:
    folder_t(std::string_view key) noexcept;
    folder_t(const uuid_t &uuid) noexcept;

    device_ptr_t device;
    folder_infos_map_t folder_infos;
    cluster_t *cluster = nullptr;
    char key[data_length];
};

struct SYNCSPIRIT_API folders_map_t : generic_map_t<folder_ptr_t, 2> {
    folder_ptr_t by_id(std::string_view id) const noexcept;
};

} // namespace syncspirit::model
