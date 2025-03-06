// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include "utils/bytes.h"
#include "misc/augmentation.hpp"
#include "misc/map.hpp"
#include "syncspirit-export.h"
#include <boost/outcome.hpp>

namespace syncspirit::model {

namespace outcome = boost::outcome_v2;

struct ignored_folder_t;

using ignored_folder_ptr_t = intrusive_ptr_t<ignored_folder_t>;

struct SYNCSPIRIT_API ignored_folder_t final : augmentable_t<ignored_folder_t> {
    static outcome::result<ignored_folder_ptr_t> create(std::string_view id, std::string_view label) noexcept;
    static outcome::result<ignored_folder_ptr_t> create(utils::bytes_view_t key, utils::bytes_view_t data) noexcept;

    utils::bytes_view_t get_key() const noexcept;
    utils::bytes_view_t get_id() const noexcept;
    std::string_view get_label() const noexcept;
    utils::bytes_t serialize() noexcept;

  private:
    ignored_folder_t(std::string_view folder_id, std::string_view label) noexcept;
    ignored_folder_t(utils::bytes_view_t key) noexcept;
    outcome::result<void> assign_fields(utils::bytes_view_t data) noexcept;

    std::string label;
    utils::bytes_t key;
};

struct SYNCSPIRIT_API ignored_folders_map_t: generic_map_t<ignored_folder_ptr_t, 1>
{
    ignored_folder_ptr_t by_key(utils::bytes_view_t key) const noexcept;
};

} // namespace syncspirit::model
