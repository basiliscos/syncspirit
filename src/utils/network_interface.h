// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#pragma once

#include <boost/asio.hpp>
#include "uri.h"
#include "log.h"
#include "syncspirit-export.h"

namespace syncspirit::utils {
using tcp = boost::asio::ip::tcp;

SYNCSPIRIT_API uri_container_t local_interfaces(const tcp::endpoint &fallback, logger_t &log) noexcept;
} // namespace syncspirit::utils
