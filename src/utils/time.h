// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#pragma once

#include <boost/date_time/posix_time/posix_time.hpp>
#include <cstdint>
#include "syncspirit-export.h"

namespace syncspirit::utils {

namespace pt = boost::posix_time;

SYNCSPIRIT_API std::int64_t as_seconds(const pt::ptime &t) noexcept;

} // namespace syncspirit::utils
