// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#pragma once
#include <string>
#include <boost/outcome.hpp>
#include <boost/filesystem.hpp>

namespace syncspirit {
namespace utils {

namespace outcome = boost::outcome_v2;
namespace fs = boost::filesystem;

outcome::result<fs::path> get_default_config_dir() noexcept;

} // namespace utils
} // namespace syncspirit
