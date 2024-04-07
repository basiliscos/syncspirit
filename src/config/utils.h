// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once
#include <cstdint>
#include <boost/outcome.hpp>
#include "main.h"
#include "syncspirit-export.h"

namespace syncspirit::config {

namespace outcome = boost::outcome_v2;

// comparators

using config_result_t = outcome::outcome<main_t, std::string>;

SYNCSPIRIT_API config_result_t get_config(std::istream &config, const boost::filesystem::path &config_path);

SYNCSPIRIT_API outcome::result<main_t> generate_config(const boost::filesystem::path &config_path);

SYNCSPIRIT_API outcome::result<void> serialize(const main_t cfg, std::ostream &out) noexcept;

} // namespace syncspirit::config
