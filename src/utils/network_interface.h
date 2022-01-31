#pragma once

#include <boost/asio.hpp>
#include "uri.h"
#include "log.h"

namespace syncspirit::utils {
using tcp = boost::asio::ip::tcp;

uri_container_t local_interfaces(const tcp::endpoint &fallback, logger_t &log) noexcept;
} // namespace syncspirit::utils
