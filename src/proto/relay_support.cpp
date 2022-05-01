// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2022 Ivan Baidakou

#include "relay_support.h"
#include <boost/endian/arithmetic.hpp>
#include <boost/endian/conversion.hpp>

namespace be = boost::endian;

template <class> inline constexpr bool always_false_v = false;

namespace syncspirit::proto::relay {

struct header_t {
    uint32_t magic;
    uint32_t type;
    uint32_t length;
};

enum class type_t {
    ping = 0,
    pong = 1,
    join_relay_request = 2,
    join_session_request = 3,
    response = 4,
    connect_request = 5,
    session_invitation = 6,
    LAST = session_invitation,
};

static constexpr auto header_sz = sizeof(header_t);
static constexpr auto max_packet_sz = size_t{1400};
static constexpr uint32_t magic = 0x9E79BC40;

static void serialize_header(char *ptr, type_t type, size_t payload_sz) noexcept {
    auto h = header_t{be::native_to_big(magic), be::native_to_big(static_cast<uint32_t>(type)),
                      be::native_to_big(static_cast<uint32_t>(payload_sz))};
    auto in = reinterpret_cast<const char *>(&h);
    std::copy(in, in + header_sz, ptr);
}

size_t serialize(const message_t &msg, std::string &out) noexcept {
    return std::visit(
        [&](auto &it) -> size_t {
            using T = std::decay_t<decltype(it)>;
            if constexpr (std::is_same_v<T, ping_t>) {
                out.resize(header_sz);
                serialize_header(out.data(), type_t::ping, 0);
                return header_sz;
            } else if constexpr (std::is_same_v<T, pong_t>) {
                out.resize(header_sz);
                serialize_header(out.data(), type_t::pong, 0);
                return header_sz;
            } else if constexpr (std::is_same_v<T, join_relay_request_t>) {
                out.resize(header_sz);
                serialize_header(out.data(), type_t::join_relay_request, 0);
                return header_sz;
            } else if constexpr (std::is_same_v<T, join_session_request_t>) {
                auto key_sz = it.key.size();
                auto payload_sz = sizeof(uint32_t) + key_sz;
                auto sz = header_sz + payload_sz;
                out.resize(sz);
                auto ptr = out.data();
                serialize_header(ptr, type_t::join_session_request, payload_sz);
                ptr += header_sz;
                *(reinterpret_cast<uint32_t *>(ptr)) = be::native_to_big((uint32_t)key_sz);
                ptr += sizeof(uint32_t);
                std::copy(it.key.begin(), it.key.end(), ptr);
                return sz;
            } else if constexpr (std::is_same_v<T, response_t>) {
                auto msg_sz = it.details.size();
                auto payload_sz = sizeof(uint32_t) + sizeof(uint32_t) + msg_sz;
                auto sz = header_sz + payload_sz;
                out.resize(sz);
                auto ptr = out.data();
                serialize_header(ptr, type_t::response, payload_sz);
                ptr += header_sz;
                *(reinterpret_cast<std::int32_t *>(ptr)) = be::native_to_big(it.code);
                ptr += sizeof(int32_t);
                *(reinterpret_cast<uint32_t *>(ptr)) = be::native_to_big((uint32_t)msg_sz);
                ptr += sizeof(uint32_t);
                std::copy(it.details.begin(), it.details.end(), ptr);
                return sz;
            } else if constexpr (std::is_same_v<T, connect_request_t>) {
                auto id_sz = it.device_id.size();
                auto payload_sz = sizeof(uint32_t) + id_sz;
                auto sz = header_sz + payload_sz;
                out.resize(sz);
                auto ptr = out.data();
                serialize_header(ptr, type_t::connect_request, payload_sz);
                ptr += header_sz;
                *(reinterpret_cast<uint32_t *>(ptr)) = be::native_to_big((uint32_t)id_sz);
                ptr += sizeof(uint32_t);
                std::copy(it.device_id.begin(), it.device_id.end(), ptr);
                return sz;
            } else if constexpr (std::is_same_v<T, session_invitation_t>) {
                auto &from = it.from;
                auto &key = it.key;
                auto &address = it.address;
                auto payload_sz = sizeof(uint32_t) * 6 + from.size() + key.size() + address.size();
                auto sz = header_sz + payload_sz;
                out.resize(sz);
                auto ptr = out.data();
                serialize_header(ptr, type_t::session_invitation, payload_sz);
                ptr += header_sz;
                *(reinterpret_cast<uint32_t *>(ptr)) = be::native_to_big((uint32_t)from.size());
                ptr += sizeof(uint32_t);
                std::copy(from.begin(), from.end(), ptr);
                ptr += from.size();

                *(reinterpret_cast<uint32_t *>(ptr)) = be::native_to_big((uint32_t)key.size());
                ptr += sizeof(uint32_t);
                std::copy(key.begin(), key.end(), ptr);
                ptr += key.size();

                *(reinterpret_cast<uint32_t *>(ptr)) = be::native_to_big((uint32_t)address.size());
                ptr += sizeof(uint32_t);
                std::copy(address.begin(), address.end(), ptr);
                ptr += address.size();

                *(reinterpret_cast<uint32_t *>(ptr)) = be::native_to_big((uint32_t)it.port);
                ptr += sizeof(uint32_t);

                *(reinterpret_cast<uint32_t *>(ptr)) = be::native_to_big((uint32_t)it.server_socket);
                return sz;
            } else {
                static_assert(always_false_v<T>, "non-exhaustive visitor!");
            }
        },
        msg);
}

static parse_result_t parse_ping(std::string_view data) noexcept { return wrapped_message_t{header_sz, ping_t{}}; }

static parse_result_t parse_pong(std::string_view data) noexcept { return wrapped_message_t{header_sz, pong_t{}}; }

static parse_result_t parse_join_relay_request(std::string_view data) noexcept {
    return wrapped_message_t{header_sz, join_relay_request_t{}};
}

static parse_result_t parse_join_session_request(std::string_view data) noexcept {
    if (data.size() < sizeof(uint32_t)) {
        return protocol_error_t{};
    }
    auto ptr = data.data();
    auto sz = be::big_to_native(*reinterpret_cast<const uint32_t *>(ptr));
    if (sz + sizeof(uint32_t) != data.size()) {
        return protocol_error_t{};
    }
    auto tail = data.substr(sizeof(uint32_t));
    return wrapped_message_t{header_sz + data.size(), join_session_request_t{std::string(tail)}};
}

static parse_result_t parse_response(std::string_view data) noexcept {
    if (data.size() < sizeof(uint32_t) * 2) {
        return protocol_error_t{};
    }
    auto ptr = data.data();
    auto code = be::big_to_native(*reinterpret_cast<const uint32_t *>(ptr));
    ptr += sizeof(uint32_t);
    auto sz = be::big_to_native(*reinterpret_cast<const uint32_t *>(ptr));
    if (sz + sizeof(uint32_t) * 2 != data.size()) {
        return protocol_error_t{};
    }
    auto tail = data.substr(sizeof(uint32_t) * 2);
    return wrapped_message_t{header_sz + data.size(), response_t{code, std::string(tail)}};
}

static parse_result_t parse_connect_request(std::string_view data) noexcept {
    if (data.size() < sizeof(uint32_t)) {
        return protocol_error_t{};
    }
    auto ptr = data.data();
    auto sz = be::big_to_native(*reinterpret_cast<const uint32_t *>(ptr));
    if (sz + sizeof(uint32_t) != data.size()) {
        return protocol_error_t{};
    }
    auto tail = data.substr(sizeof(uint32_t));
    return wrapped_message_t{header_sz + data.size(), connect_request_t{std::string(tail)}};
}

static parse_result_t parse_session_invitation(std::string_view data) noexcept {
    if (data.size() < sizeof(uint32_t)) {
        return protocol_error_t{};
    }
    auto orig = data;

    auto from_sz = be::big_to_native(*reinterpret_cast<const uint32_t *>(data.data()));
    data = data.substr(sizeof(uint32_t));
    if (data.size() < from_sz) {
        return protocol_error_t{};
    }
    auto from = data.substr(0, from_sz);
    data = data.substr(from_sz);

    if (data.size() < sizeof(uint32_t)) {
        return protocol_error_t{};
    }
    auto key_sz = be::big_to_native(*reinterpret_cast<const uint32_t *>(data.data()));
    data = data.substr(sizeof(uint32_t));
    if (data.size() < key_sz) {
        return protocol_error_t{};
    }
    auto key = data.substr(0, key_sz);
    data = data.substr(key_sz);

    if (data.size() < sizeof(uint32_t)) {
        return protocol_error_t{};
    }
    auto addr_sz = be::big_to_native(*reinterpret_cast<const uint32_t *>(data.data()));
    data = data.substr(sizeof(uint32_t));
    if (data.size() < key_sz) {
        return protocol_error_t{};
    }
    auto addr = data.substr(0, addr_sz);
    data = data.substr(addr_sz);

    if (data.size() < sizeof(uint32_t)) {
        return protocol_error_t{};
    }
    auto port = be::big_to_native(*reinterpret_cast<const uint32_t *>(data.data()));
    data = data.substr(sizeof(uint32_t));

    if (data.size() < sizeof(uint32_t)) {
        return protocol_error_t{};
    }
    auto server_socket = be::big_to_native(*reinterpret_cast<const uint32_t *>(data.data()));
    data = data.substr(sizeof(uint32_t));

    return wrapped_message_t{
        header_sz + orig.size(),
        session_invitation_t{std::string(from), std::string(key), std::string(addr), port, (bool)server_socket}};
}

parse_result_t parse(std::string_view data) noexcept {
    if (data.size() < header_sz) {
        return incomplete_t{};
    }
    auto ptr = data.data();
    auto header = reinterpret_cast<const header_t *>(ptr);
    if (be::big_to_native(header->magic) != magic) {
        return protocol_error_t{};
    }
    auto type = be::big_to_native(header->type);
    if (type > (uint32_t)type_t::LAST) {
        return protocol_error_t{};
    }
    auto sz = be::big_to_native(header->length);
    if (sz > max_packet_sz) {
        return protocol_error_t{};
    }
    if (data.size() < header_sz + sz) {
        return incomplete_t{};
    }
    auto tail = data.substr(header_sz);

    switch ((type_t)type) {
    case type_t::ping:
        return parse_ping(tail);
    case type_t::pong:
        return parse_pong(tail);
    case type_t::join_relay_request:
        return parse_join_relay_request(tail);
    case type_t::join_session_request:
        return parse_join_session_request(tail);
    case type_t::response:
        return parse_response(tail);
    case type_t::connect_request:
        return parse_connect_request(tail);
    case type_t::session_invitation:
        return parse_session_invitation(tail);
    default:
        return protocol_error_t{};
    }
}

} // namespace syncspirit::proto::relay
