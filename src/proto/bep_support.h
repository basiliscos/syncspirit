#pragma once
#include <fmt/format.h>
#include <boost/outcome.hpp>

namespace syncspirit::proto {

namespace outcome = boost::outcome_v2;

void make_hello_message(fmt::memory_buffer &buff, const std::string_view &device_name) noexcept;

} // namespace syncspirit::proto
