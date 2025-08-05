// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2022-2025 Ivan Baidakou

#include "relay_support.h"
#include "utils/error_code.h"
#include <nlohmann/json.hpp>
#include <boost/endian/arithmetic.hpp>
#include <boost/endian/conversion.hpp>
#include <ares.h>
#include <cstring>

namespace be = boost::endian;
using json = nlohmann::json;

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

static pt::time_duration parse_interval(const std::string_view in) noexcept {
    auto ptr = in.data();
    auto end = in.data() + in.size();
    auto s = ptr;
    auto e = ptr;
    while (ptr < end && *ptr != 'm') {
        ++ptr;
    }
    if (*ptr == 'm') {
        e = ptr++;
    };
    int mins = 0;
    while (s < e) {
        mins *= 10;
        mins += *s - '0';
        ++s;
    }

    s = e = ptr;
    while (ptr < end && *ptr != 's') {
        ++ptr;
    }
    if (*ptr == 's') {
        e = ptr;
    };

    int secs = 0;
    while (s < e) {
        secs *= 10;
        secs += *s - '0';
        ++s;
    }

    if (mins == 0 && secs == 0) {
        mins = 1;
    }

    return pt::minutes{mins} + pt::seconds{secs};
}

static void serialize_header(unsigned char *ptr, type_t type, size_t payload_sz) noexcept {
    auto h = header_t{be::native_to_big(magic), be::native_to_big(static_cast<uint32_t>(type)),
                      be::native_to_big(static_cast<uint32_t>(payload_sz))};
    auto in = reinterpret_cast<const char *>(&h);
    std::copy(in, in + header_sz, ptr);
}

static inline void write32_be(void *ptr, std::uint32_t value) {
    auto v = be::native_to_big(value);
    std::memcpy(ptr, &v, sizeof(value));
}

static inline std::uint32_t read32_be(const void *ptr) {
    std::uint32_t value;
    std::memcpy(&value, ptr, sizeof(value));
    return be::big_to_native(value);
}

size_t serialize(const message_t &msg, utils::bytes_t &out) noexcept {
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
                auto address_raw = std::uint32_t{0};
                if (it.address.has_value()) {
                    address_raw = be::native_to_big(it.address->to_uint());
                }
                auto address = std::string_view(reinterpret_cast<char *>(&address_raw), 4);
                auto payload_sz = sizeof(uint32_t) * 6 + from.size() + key.size() + address.size();
                auto sz = header_sz + payload_sz;
                out.resize(sz);
                auto ptr = out.data();
                serialize_header(ptr, type_t::session_invitation, payload_sz);
                ptr += header_sz;
                write32_be(ptr, (uint32_t)from.size());
                ptr += sizeof(uint32_t);
                std::copy(from.begin(), from.end(), ptr);
                ptr += from.size();

                write32_be(ptr, (uint32_t)key.size());
                ptr += sizeof(uint32_t);
                std::copy(key.begin(), key.end(), ptr);
                ptr += key.size();

                write32_be(ptr, (uint32_t)address.size());
                ptr += sizeof(uint32_t);
                std::copy(address.begin(), address.end(), ptr);
                ptr += address.size();

                write32_be(ptr, (uint32_t)it.port);
                ptr += sizeof(uint32_t);

                write32_be(ptr, (uint32_t)it.server_socket);
                return sz;
            } else {
                static_assert(always_false_v<T>, "non-exhaustive visitor!");
            }
        },
        msg);
}

static parse_result_t parse_ping(utils::bytes_view_t) noexcept { return wrapped_message_t{header_sz, ping_t{}}; }

static parse_result_t parse_pong(utils::bytes_view_t) noexcept { return wrapped_message_t{header_sz, pong_t{}}; }

static parse_result_t parse_join_relay_request(utils::bytes_view_t) noexcept {
    return wrapped_message_t{header_sz, join_relay_request_t{}};
}

static parse_result_t parse_join_session_request(utils::bytes_view_t data) noexcept {
    if (data.size() < sizeof(uint32_t)) {
        return protocol_error_t{};
    }
    auto ptr = data.data();
    auto sz = be::big_to_native(*reinterpret_cast<const uint32_t *>(ptr));
    if (sz + sizeof(uint32_t) > data.size()) {
        return protocol_error_t{};
    }
    auto tail = data.subspan(sizeof(uint32_t));
    auto tail_bytes = utils::bytes_t(tail.begin(), tail.end());
    return wrapped_message_t{header_sz + data.size(), join_session_request_t{std::move(tail_bytes)}};
}

static parse_result_t parse_response(utils::bytes_view_t data) noexcept {
    if (data.size() < sizeof(uint32_t) * 2) {
        return protocol_error_t{};
    }
    auto ptr = data.data();
    auto code = be::big_to_native(*reinterpret_cast<const uint32_t *>(ptr));
    ptr += sizeof(uint32_t);
    auto sz = be::big_to_native(*reinterpret_cast<const uint32_t *>(ptr));
    if (sz + sizeof(uint32_t) * 2 > data.size()) {
        return protocol_error_t{};
    }
    auto tail = data.subspan(sizeof(uint32_t) * 2, sz);
    auto tail_str = std::string_view((const char *)tail.data(), tail.size());
    return wrapped_message_t{header_sz + data.size(), response_t{code, std::string(tail_str)}};
}

static parse_result_t parse_connect_request(utils::bytes_view_t data) noexcept {
    if (data.size() < sizeof(uint32_t)) {
        return protocol_error_t{};
    }
    auto ptr = data.data();
    auto sz = be::big_to_native(*reinterpret_cast<const uint32_t *>(ptr));
    if (sz + sizeof(uint32_t) != data.size()) {
        return protocol_error_t{};
    }
    auto tail = data.subspan(sizeof(uint32_t));
    auto tail_bytes = utils::bytes_t(tail.begin(), tail.end());
    return wrapped_message_t{header_sz + data.size(), connect_request_t{std::move(tail_bytes)}};
}

static parse_result_t parse_session_invitation(utils::bytes_view_t data) noexcept {
    if (data.size() < sizeof(uint32_t)) {
        return protocol_error_t{};
    }
    auto orig = data;

    auto from_sz = read32_be(data.data());
    data = data.subspan(sizeof(uint32_t));
    if (data.size() < from_sz) {
        return protocol_error_t{};
    }
    auto from = data.subspan(0, from_sz);
    data = data.subspan(from_sz);

    if (data.size() < sizeof(uint32_t)) {
        return protocol_error_t{};
    }
    auto key_sz = read32_be(data.data());
    data = data.subspan(sizeof(uint32_t));
    if (data.size() < key_sz) {
        return protocol_error_t{};
    }
    auto key = data.subspan(0, key_sz);
    data = data.subspan(key_sz);

    if (data.size() < sizeof(uint32_t)) {
        return protocol_error_t{};
    }
    auto addr_sz = read32_be(data.data());
    data = data.subspan(sizeof(uint32_t));
    if (data.size() < addr_sz) {
        return protocol_error_t{};
    }
    auto addr = data.subspan(0, addr_sz);
    data = data.subspan(addr_sz);
    auto ip = ipv4_option_t{};
    if (addr_sz == 4) {
        auto number = std::uint32_t{0};
        std::memcpy(&number, addr.data(), addr_sz);
        if (number) {
            ip = boost::asio::ip::address_v4(be::big_to_native(number));
        }
    }
    if (data.size() < sizeof(uint32_t)) {
        return protocol_error_t{};
    }
    auto port = read32_be(data.data());
    data = data.subspan(sizeof(uint32_t));

    if (data.size() < sizeof(uint32_t)) {
        return protocol_error_t{};
    }
    auto server_socket = read32_be(data.data());
    data = data.subspan(sizeof(uint32_t));

    if (!addr.empty() && !addr[0]) {
        addr = {};
    };

    auto from_bytes = utils::bytes_t(from.begin(), from.end());
    auto key_bytes = utils::bytes_t(key.begin(), key.end());
    return wrapped_message_t{header_sz + orig.size(), session_invitation_t{std::move(from_bytes), std::move(key_bytes),
                                                                           ip, port, (bool)server_socket}};
}

parse_result_t parse(utils::bytes_view_t data) noexcept {
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
    auto tail = data.subspan(header_sz);

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

std::optional<model::device_id_t> parse_device(const utils::uri_ptr_t &uri) noexcept {
    if (uri->scheme() != "relay") {
        return {};
    }
    auto device_id_str = std::string{};
    auto params = uri->params();
    for (auto p : params) {
        if (p.key == "id") {
            device_id_str = p.value;
            break;
        }
    }
    if (device_id_str.empty()) {
        return {};
    }
    auto device_opt = model::device_id_t::from_string(device_id_str);
    if (!device_opt) {
        return {};
    }
    return std::move(device_opt.value());
}

outcome::result<relay_infos_t> parse_endpoint(std::string_view buff) noexcept {
    using namespace syncspirit::utils;
    auto data = json::parse(buff.begin(), buff.end(), nullptr, false);
    if (data.is_discarded()) {
        return make_error_code(error_code_t::malformed_json);
    }
    if (!data.is_object()) {
        return make_error_code(error_code_t::incorrect_json);
    }

    auto &relays = data["relays"];
    if (!relays.is_array()) {
        return make_error_code(error_code_t::incorrect_json);
    }

    auto r = relay_infos_t{};
    for (auto &it : relays) {
        if (!it.is_object()) {
            continue;
            return make_error_code(error_code_t::incorrect_json);
        }
        auto &url = it["url"];
        if (!url.is_string()) {
            continue;
        }
        auto uri_str = url.get<std::string>();
        auto uri = utils::parse(uri_str.c_str());
        if (!uri) {
            continue;
        }
        if (uri->scheme() != "relay") {
            continue;
        }
        auto device_id_str = std::string{};
        auto ping_interval_str = std::string{};
        auto params = uri->params();
        for (auto p : params) {
            if (p.key == "id") {
                device_id_str = p.value;
            } else if (p.key == "pingInterval") {
                ping_interval_str = p.value;
            }
        }
        if (device_id_str.empty()) {
            continue;
        }
        auto device_id = model::device_id_t::from_string(device_id_str);
        if (!device_id) {
            continue;
        }

        auto ping_interval = parse_interval(ping_interval_str);

        auto location = location_t{};
        auto &j_location = it["location"];
        if (j_location.is_object()) {
            auto &latitude = j_location["latitude"];
            if (latitude.is_number_float()) {
                location.latitude = latitude.get<float>();
            }
            auto &longitude = j_location["longitude"];
            if (longitude.is_number_float()) {
                location.longitude = longitude.get<float>();
            }
            auto &city = j_location["city"];
            if (city.is_string()) {
                location.city = city.get<std::string>();
            }
            auto &country = j_location["country"];
            if (country.is_string()) {
                location.country = country.get<std::string>();
            }
            auto &continent = j_location["continent"];
            if (continent.is_string()) {
                location.continent = continent.get<std::string>();
            }
        }
        auto relay =
            relay_info_ptr_t{new relay_info_t{std::move(uri), device_id.value(), std::move(location), ping_interval}};
        r.emplace_back(std::move(relay));
    }
    return r;
}

} // namespace syncspirit::proto::relay
