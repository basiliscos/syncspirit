#pragma once
#include "uri.h"
#include <fmt/format.h>
#include <string>
#include <vector>
#include <boost/outcome.hpp>

namespace syncspirit::utils {

namespace outcome = boost::outcome_v2;

struct upnp_fields {
    static const char *st;
    static const char *man;
    static const char *mx;
    static const char *usn;
};

extern const char *upnp_addr;
constexpr std::uint16_t upnp_port = 1900;

struct discovery_result {
    URI location;
    std::string search_target;
    std::string usn;
};

struct igd_result {
    std::string control_path;
    std::string description_path;
};

outcome::result<discovery_result> parse(const char *data, std::size_t bytes) noexcept;

outcome::result<void> make_discovery_request(fmt::memory_buffer &buff, std::uint32_t max_wait) noexcept;

outcome::result<void> make_description_request(fmt::memory_buffer &buff, const URI &uri) noexcept;

outcome::result<igd_result> parse_igd(const char *data, std::size_t bytes) noexcept;

outcome::result<void> make_external_ip_request(fmt::memory_buffer &buff, const URI &uri) noexcept;

outcome::result<std::string> parse_external_ip(const char *data, std::size_t bytes) noexcept;

} // namespace syncspirit::utils
