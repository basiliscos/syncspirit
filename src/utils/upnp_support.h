#pragma once
#include "uri.h"
#include <fmt/format.h>
#include <outcome.hpp>
#include <string>
#include <vector>

namespace syncspirit::utils {

namespace outcome = OUTCOME_V2_NAMESPACE;

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

outcome::result<discovery_result> parse(const std::uint8_t *data, std::size_t bytes) noexcept;

outcome::result<void> make_discovery_request(fmt::memory_buffer &buff, std::uint32_t max_wait) noexcept;

outcome::result<void> make_description_request(fmt::memory_buffer &buff, const discovery_result &dr) noexcept;

} // namespace syncspirit::utils
