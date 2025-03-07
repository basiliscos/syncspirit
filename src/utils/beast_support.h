// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include <boost/beast/http.hpp>
#include <boost/outcome.hpp>
#include "utils/bytes.h"
#include "syncspirit-export.h"

namespace syncspirit::utils {
namespace sys = boost::system;
namespace outcome = boost::outcome_v2;
namespace http = boost::beast::http;

SYNCSPIRIT_API outcome::result<void> serialize(http::request<http::empty_body> &req, utils::bytes_t &buff);

SYNCSPIRIT_API outcome::result<void> serialize(http::request<http::string_body> &req, utils::bytes_t &buff);

} // namespace syncspirit::utils
