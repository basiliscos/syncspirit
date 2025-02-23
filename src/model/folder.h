// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include <cstdint>
#include <optional>
#include <filesystem>
#include <boost/outcome.hpp>
#include "misc/augmentation.hpp"
#include "device.h"
#include "folder_info.h"
#include "misc/uuid.h"
#include "folder_data.h"
#include "syncspirit-export.h"
#include "proto/proto-fwd.hpp"

namespace syncspirit::model {

namespace bfs = std::filesystem;
namespace outcome = boost::outcome_v2;

struct cluster_t;
using cluster_ptr_t = intrusive_ptr_t<cluster_t>;

struct folder_t;

using folder_ptr_t = intrusive_ptr_t<folder_t>;

struct SYNCSPIRIT_API folder_t final : augmentable_t<folder_t>, folder_data_t {
    static outcome::result<folder_ptr_t> create(utils::bytes_view_t key, const db::Folder &folder) noexcept;
    static outcome::result<folder_ptr_t> create(const bu::uuid &uuid, const db::Folder &folder) noexcept;

    using folder_data_t::assign_fields;
    using folder_data_t::serialize;

    void assign_cluster(const cluster_ptr_t &cluster) noexcept;
    void add(const folder_info_ptr_t &folder_info) noexcept;
    utils::bytes_t serialize() noexcept;

    bool operator==(const folder_t &other) const noexcept { return get_id() == other.get_id(); }
    bool operator!=(const folder_t &other) const noexcept { return !(*this == other); }

    folder_info_ptr_t is_shared_with(const device_t &device) const noexcept;

    utils::bytes_view_t get_key() const noexcept { return utils::bytes_view_t(key, data_length); }
    utils::bytes_view_t get_uuid() const noexcept;
    inline auto &get_folder_infos() noexcept { return folder_infos; }
    inline auto &get_folder_infos() const noexcept { return folder_infos; }
    inline cluster_t *&get_cluster() noexcept { return cluster; }
    const pt::ptime &get_scan_start() const noexcept;
    void set_scan_start(const pt::ptime &value) noexcept;
    const pt::ptime &get_scan_finish() noexcept;
    void set_scan_finish(const pt::ptime &value) noexcept;
    bool is_scanning() const noexcept;
    bool is_synchronizing() const noexcept;
    void adjust_synchronization(std::int_fast32_t delta) noexcept;
    void mark_suspended(bool value) noexcept;
    bool is_suspended() const noexcept;

    using folder_data_t::get_path;
    using folder_data_t::set_path;
    std::optional<proto::Folder> generate(const model::device_t &device) const noexcept;

    template <typename T> auto &access() noexcept;
    template <typename T> auto &access() const noexcept;

    static const constexpr size_t data_length = uuid_length + 1;

  private:
    folder_t(utils::bytes_view_t key) noexcept;
    folder_t(const bu::uuid &uuid) noexcept;

    pt::ptime scan_start;
    pt::ptime scan_finish;
    device_ptr_t device;
    folder_infos_map_t folder_infos;
    cluster_t *cluster = nullptr;
    unsigned char key[data_length];
    std::int_fast32_t synchronizing = 0;
    bool suspended;
};

struct SYNCSPIRIT_API folders_map_t : generic_map_t<folder_ptr_t, 2> {
    folder_ptr_t by_key(utils::bytes_view_t id) const noexcept;
    folder_ptr_t by_id(std::string_view id) const noexcept;
};

} // namespace syncspirit::model
