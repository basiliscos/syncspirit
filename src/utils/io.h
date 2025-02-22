// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include <boost/nowide/fstream.hpp>

namespace syncspirit::utils {

using fstream_t = boost::nowide::fstream;
using ifstream_t = boost::nowide::ifstream;
using ofstream_t = boost::nowide::ofstream;

} // namespace syncspirit::utils
