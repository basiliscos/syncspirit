// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include <string>
#include <boost/outcome.hpp>
#include <filesystem>
#include "syncspirit-export.h"

namespace syncspirit {
namespace utils {

namespace outcome = boost::outcome_v2;
namespace bfs = std::filesystem;

using home_option_t = outcome::result<bfs::path>;

SYNCSPIRIT_API outcome::result<bfs::path> get_home_dir() noexcept;

SYNCSPIRIT_API outcome::result<bfs::path> get_default_config_dir() noexcept;

SYNCSPIRIT_API std::string expand_home(const std::string &path, const home_option_t &home) noexcept;

} // namespace utils
} // namespace syncspirit
