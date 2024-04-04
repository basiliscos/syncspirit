// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include <string>
#include <boost/outcome.hpp>
#include <boost/filesystem.hpp>
#include "syncspirit-export.h"

namespace syncspirit {
namespace utils {

namespace outcome = boost::outcome_v2;
namespace fs = boost::filesystem;

SYNCSPIRIT_API outcome::result<fs::path> get_home_dir() noexcept;

SYNCSPIRIT_API outcome::result<fs::path> get_default_config_dir() noexcept;

} // namespace utils
} // namespace syncspirit
