// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once
#include "utils/bytes.h"
#include "utils/uri.h"
#include "model/misc/upnp.h"
#include "syncspirit-export.h"
#include <string>
#include <boost/outcome.hpp>

namespace syncspirit::proto {

namespace outcome = boost::outcome_v2;

struct upnp_fields {
    static const char *st;
    static const char *man;
    static const char *mx;
    static const char *usn;
};

extern const char *upnp_addr;
constexpr std::uint16_t upnp_port = 1900;

SYNCSPIRIT_API outcome::result<model::discovery_result> parse(const char *data, std::size_t bytes) noexcept;

SYNCSPIRIT_API outcome::result<void> make_discovery_request(utils::bytes_t &buff, std::uint32_t max_wait) noexcept;

SYNCSPIRIT_API outcome::result<void> make_description_request(utils::bytes_t &buff,
                                                              const utils::uri_ptr_t &uri) noexcept;

SYNCSPIRIT_API outcome::result<model::igd_result> parse_igd(const char *data, std::size_t bytes) noexcept;

SYNCSPIRIT_API outcome::result<void> make_external_ip_request(utils::bytes_t &buff,
                                                              const utils::uri_ptr_t &uri) noexcept;

SYNCSPIRIT_API outcome::result<std::string> parse_external_ip(const char *data, std::size_t bytes) noexcept;

SYNCSPIRIT_API outcome::result<void> make_mapping_request(utils::bytes_t &buff, const utils::uri_ptr_t &uri,
                                                          std::uint16_t external_port, const std::string &internal_ip,
                                                          std::uint16_t internal_port) noexcept;

SYNCSPIRIT_API outcome::result<void> make_unmapping_request(utils::bytes_t &buff, const utils::uri_ptr_t &uri,
                                                            std::uint16_t external_port) noexcept;

SYNCSPIRIT_API outcome::result<void> make_mapping_validation_request(utils::bytes_t &buff,
                                                                     const utils::uri_ptr_t &uri,
                                                                     std::uint16_t external_port) noexcept;

SYNCSPIRIT_API outcome::result<bool> parse_mapping(const char *data, std::size_t bytes) noexcept;

SYNCSPIRIT_API outcome::result<bool> parse_unmapping(const char *data, std::size_t bytes) noexcept;

SYNCSPIRIT_API outcome::result<bool> parse_mapping_validation(const char *data, std::size_t bytes) noexcept;

} // namespace syncspirit::proto
