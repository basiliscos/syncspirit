// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#pragma once
#include <cstdint>
#include <boost/outcome.hpp>
#include "main.h"
#include "syncspirit-export.h"

namespace syncspirit::config {

namespace outcome = boost::outcome_v2;

// comparators

SYNCSPIRIT_API bool operator==(const bep_config_t &lhs, const bep_config_t &rhs) noexcept;
SYNCSPIRIT_API bool operator==(const dialer_config_t &lhs, const dialer_config_t &rhs) noexcept;
SYNCSPIRIT_API bool operator==(const fs_config_t &lhs, const fs_config_t &rhs) noexcept;
SYNCSPIRIT_API bool operator==(const db_config_t &lhs, const db_config_t &rhs) noexcept;
SYNCSPIRIT_API bool operator==(const global_announce_config_t &lhs, const global_announce_config_t &rhs) noexcept;
SYNCSPIRIT_API bool operator==(const local_announce_config_t &lhs, const local_announce_config_t &rhs) noexcept;
SYNCSPIRIT_API bool operator==(const main_t &lhs, const main_t &rhs) noexcept;
SYNCSPIRIT_API bool operator==(const upnp_config_t &lhs, const upnp_config_t &rhs) noexcept;

// misc

using config_result_t = outcome::outcome<main_t, std::string>;

SYNCSPIRIT_API config_result_t get_config(std::istream &config, const boost::filesystem::path &config_path);

SYNCSPIRIT_API outcome::result<main_t> generate_config(const boost::filesystem::path &config_path);

SYNCSPIRIT_API outcome::result<void> serialize(const main_t cfg, std::ostream &out) noexcept;

} // namespace syncspirit::config
