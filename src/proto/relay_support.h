// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2022-2024 Ivan Baidakou

#pragma once

#include <string>
#include <cstdint>
#include <variant>
#include <vector>
#include <optional>
#include <model/misc/arc.hpp>
#include <model/device_id.h>
#include "utils/uri.h"
#include "syncspirit-export.h"
#include <boost/outcome.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

namespace syncspirit::proto::relay {

namespace outcome = boost::outcome_v2;
namespace pt = boost::posix_time;

struct ping_t {};

struct pong_t {};

struct join_relay_request_t {};

struct join_session_request_t {
    std::string key;
};

struct response_t {
    std::uint32_t code;
    std::string details;
};

struct connect_request_t {
    std::string device_id;
};

struct session_invitation_t {
    std::string from;
    std::string key;
    std::string address;
    std::uint32_t port;
    bool server_socket;
};

using message_t = std::variant<ping_t, pong_t, join_relay_request_t, join_session_request_t, response_t,
                               connect_request_t, session_invitation_t>;

// support
struct incomplete_t {};
struct protocol_error_t {};
struct wrapped_message_t {
    size_t length;
    message_t message;
};

using parse_result_t = std::variant<incomplete_t, protocol_error_t, wrapped_message_t>;

SYNCSPIRIT_API size_t serialize(const relay::message_t &, std::string &out) noexcept;
SYNCSPIRIT_API parse_result_t parse(std::string_view data) noexcept;

struct location_t {
    float latitude;
    float longitude;
    std::string city;
    std::string country;
    std::string continent;
};

struct relay_info_t : model::arc_base_t<relay_info_t> {
    inline relay_info_t(utils::uri_ptr_t uri_, const model::device_id_t &device_id_, location_t location_,
                        const pt::time_duration &ping_interval_) noexcept
        : uri(std::move(uri_)), device_id{device_id_}, location{std::move(location_)}, ping_interval{ping_interval_} {}
    utils::uri_ptr_t uri;
    model::device_id_t device_id;
    location_t location;
    pt::time_duration ping_interval;
};

using relay_info_ptr_t = model::intrusive_ptr_t<relay_info_t>;
using relay_infos_t = std::vector<relay_info_ptr_t>;

SYNCSPIRIT_API std::optional<model::device_id_t> parse_device(const utils::uri_ptr_t &uri) noexcept;
SYNCSPIRIT_API outcome::result<relay_infos_t> parse_endpoint(std::string_view data) noexcept;

} // namespace syncspirit::proto::relay
