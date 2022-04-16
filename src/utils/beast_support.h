// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#pragma once

#include <boost/beast/http.hpp>
#include <boost/outcome.hpp>
#include <fmt/fmt.h>
#include "syncspirit-export.h"

namespace syncspirit::utils {
namespace sys = boost::system;
namespace outcome = boost::outcome_v2;
namespace http = boost::beast::http;

SYNCSPIRIT_API outcome::result<void> serialize(http::request<http::empty_body> &req, fmt::memory_buffer &buff);

SYNCSPIRIT_API outcome::result<void> serialize(http::request<http::string_body> &req, fmt::memory_buffer &buff);

} // namespace syncspirit::utils
