#pragma once
#include <boost/beast/http.hpp>
#include <boost/outcome.hpp>
#include <fmt/format.h>

namespace syncspirit::utils {
namespace sys = boost::system;
namespace outcome = boost::outcome_v2;
namespace http = boost::beast::http;


outcome::result<void> serialize(http::request<http::empty_body>& req, fmt::memory_buffer &buff);

outcome::result<void> serialize(http::request<http::string_body>& req, fmt::memory_buffer &buff);

}
