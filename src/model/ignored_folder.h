// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#pragma once

#include "misc/arc.hpp"
#include "misc/map.hpp"
#include "syncspirit-export.h"
#include <boost/outcome.hpp>

namespace syncspirit::model {

namespace outcome = boost::outcome_v2;

struct ignored_folder_t;

using ignored_folder_ptr_t = intrusive_ptr_t<ignored_folder_t>;

struct SYNCSPIRIT_API ignored_folder_t final : arc_base_t<ignored_folder_t> {
    static outcome::result<ignored_folder_ptr_t> create(std::string folder_id, std::string_view label) noexcept;
    static outcome::result<ignored_folder_ptr_t> create(std::string_view key, std::string_view data) noexcept;

    std::string_view get_key() const noexcept;
    std::string_view get_id() const noexcept;
    std::string_view get_label() const noexcept;
    std::string serialize() noexcept;

  private:
    ignored_folder_t(std::string_view folder_id, std::string_view label) noexcept;
    ignored_folder_t(std::string_view key) noexcept;
    outcome::result<void> assign_fields(std::string_view data) noexcept;

    std::string label;
    std::string key;
};

using ignored_folders_map_t = generic_map_t<ignored_folder_ptr_t, 1>;

} // namespace syncspirit::model
