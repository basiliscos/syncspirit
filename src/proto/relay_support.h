// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2022 Ivan Baidakou

#pragma once

#include <string>
#include <cstdint>
#include <variant>

namespace syncspirit::proto {

namespace relay {

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

} // namespace relay

size_t serialize(const relay::message_t &, std::string &out) noexcept;

relay::parse_result_t parse(std::string_view data) noexcept;

} // namespace syncspirit::proto
