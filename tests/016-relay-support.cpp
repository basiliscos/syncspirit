// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2023-2024 Ivan Baidakou

#include "test-utils.h"
#include "proto/relay_support.h"

using namespace syncspirit::proto::relay;

TEST_CASE("relay proto", "[relay]") {
    std::string buff;

    SECTION("successful cases") {
        SECTION("ping") {
            auto sz = serialize(ping_t{}, buff);
            REQUIRE(sz);
            auto r = parse(buff);
            auto msg = std::get_if<wrapped_message_t>(&r);
            REQUIRE(msg);
            CHECK(msg->length == sz);
            auto target = std::get_if<ping_t>(&msg->message);
            REQUIRE(target);
        }

        SECTION("pong") {
            auto sz = serialize(pong_t{}, buff);
            REQUIRE(sz);
            auto r = parse(buff);
            auto msg = std::get_if<wrapped_message_t>(&r);
            REQUIRE(msg);
            CHECK(msg->length == sz);
            auto target = std::get_if<pong_t>(&msg->message);
            REQUIRE(target);
        }

        SECTION("join_relay_request") {
            auto sz = serialize(join_relay_request_t{}, buff);
            REQUIRE(sz);
            auto r = parse(buff);
            auto msg = std::get_if<wrapped_message_t>(&r);
            REQUIRE(msg);
            CHECK(msg->length == sz);
            auto target = std::get_if<join_relay_request_t>(&msg->message);
            REQUIRE(target);
        }

        SECTION("join_session_request") {
            auto source = join_session_request_t{"lorem impsum dolor"};
            auto sz = serialize(source, buff);
            REQUIRE(sz);
            auto r = parse(buff);
            auto msg = std::get_if<wrapped_message_t>(&r);
            REQUIRE(msg);
            CHECK(msg->length == sz);
            auto target = std::get_if<join_session_request_t>(&msg->message);
            REQUIRE(target);
            CHECK(target->key == source.key);
        }

        SECTION("response") {
            auto source = response_t{404, "not found"};
            auto sz = serialize(source, buff);
            REQUIRE(sz);
            auto r = parse(buff);
            auto msg = std::get_if<wrapped_message_t>(&r);
            REQUIRE(msg);
            CHECK(msg->length == sz);
            auto target = std::get_if<response_t>(&msg->message);
            REQUIRE(target);
            CHECK(target->code == source.code);
            CHECK(target->details == source.details);
        }

        SECTION("connect_request") {
            auto source = connect_request_t{"lorem impsum dolor"};
            auto sz = serialize(source, buff);
            REQUIRE(sz);
            auto r = parse(buff);
            auto msg = std::get_if<wrapped_message_t>(&r);
            REQUIRE(msg);
            CHECK(msg->length == sz);
            auto target = std::get_if<connect_request_t>(&msg->message);
            REQUIRE(target);
            CHECK(target->device_id == source.device_id);
        }

        SECTION("session_invitation") {
            auto ip = boost::asio::ip::address_v4::from_string("127.0.0.1");
            auto source = session_invitation_t{"lorem", "impsum", ip, 1234, true};
            auto sz = serialize(source, buff);
            REQUIRE(sz);
            auto r = parse(buff);
            auto msg = std::get_if<wrapped_message_t>(&r);
            REQUIRE(msg);
            CHECK(msg->length == sz);
            auto target = std::get_if<session_invitation_t>(&msg->message);
            REQUIRE(target);
            CHECK(target->from == source.from);
            CHECK(target->key == source.key);
            CHECK(target->address.value() == ip);
            CHECK(target->server_socket == source.server_socket);
        }

        SECTION("session_invitation (host of zeroes)") {
            auto ip = boost::asio::ip::address_v4(0);
            auto source = session_invitation_t{"lorem", "impsum", ip, 1234, true};
            auto sz = serialize(source, buff);
            REQUIRE(sz);
            auto r = parse(buff);
            auto msg = std::get_if<wrapped_message_t>(&r);
            REQUIRE(msg);
            CHECK(msg->length == sz);
            auto target = std::get_if<session_invitation_t>(&msg->message);
            REQUIRE(target);
            CHECK(target->from == source.from);
            CHECK(target->key == source.key);
            CHECK(!target->address.has_value());
            CHECK(target->server_socket == source.server_socket);
        }

        SECTION("response sample") {
            const unsigned char data[] = {0x9e, 0x79, 0xbc, 0x40, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00,
                                          0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07,
                                          0x73, 0x75, 0x63, 0x63, 0x65, 0x73, 0x73, 0x00};
            auto ptr = reinterpret_cast<const char *>(data);
            auto r = parse({ptr, sizeof(data)});
            auto msg = std::get_if<wrapped_message_t>(&r);
            REQUIRE(msg);
            CHECK(msg->length == 28);
            auto target = std::get_if<response_t>(&msg->message);
            REQUIRE(target);
            CHECK(target->code == 0);
            auto details = std::string_view("success");
            CHECK(target->details == details);
        }

        SECTION("response sample-2") {
            const unsigned char data[] = {0x9e, 0x79, 0xbc, 0x40, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
                                          0x14, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x09, 0x6e, 0x6f,
                                          0x74, 0x20, 0x66, 0x6f, 0x75, 0x6e, 0x64, 0x00, 0x00, 0x00};
            auto ptr = reinterpret_cast<const char *>(data);
            auto r = parse({ptr, sizeof(data)});
            auto msg = std::get_if<wrapped_message_t>(&r);
            REQUIRE(msg);
            CHECK(msg->length == 32);
            auto target = std::get_if<response_t>(&msg->message);
            REQUIRE(target);
            CHECK(target->code == 1);
            auto details = std::string_view("not found");
            CHECK(target->details == details);
        }

        SECTION("session invitation sample") {
            const unsigned char data[] = {0x9e, 0x79, 0xbc, 0x40, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x4a, 0x00,
                                          0x00, 0x00, 0x20, 0xf8, 0x31, 0xf5, 0x75, 0xea, 0x61, 0x8a, 0x2f, 0x15, 0xef,
                                          0x67, 0x68, 0x36, 0x4a, 0x62, 0x89, 0xfc, 0x76, 0xb6, 0x73, 0xc8, 0x5a, 0x2a,
                                          0xbe, 0x60, 0x8f, 0x4a, 0xff, 0x27, 0xba, 0x39, 0x02, 0x00, 0x00, 0x00, 0x12,
                                          0x6c, 0x6f, 0x72, 0x65, 0x6d, 0x2d, 0x69, 0x6d, 0x73, 0x70, 0x75, 0x6d, 0x2d,
                                          0x64, 0x6f, 0x6c, 0x6f, 0x72, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x39,
                                          0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00};
            auto ptr = reinterpret_cast<const char *>(data);
            auto str = std::string_view(ptr, sizeof(data));
            auto r = parse(str);
            auto msg = std::get_if<wrapped_message_t>(&r);
            REQUIRE(msg);
            CHECK(msg->length == str.size());
            auto target = std::get_if<session_invitation_t>(&msg->message);
            REQUIRE(target);
            CHECK(target->from.size() == 32);
            CHECK(target->key == "lorem-imspum-dolor");
            CHECK(!target->address.has_value());
            CHECK(target->port == 12345);
            CHECK(target->server_socket);
        }
    }

    SECTION("incomplete") {
        auto r = parse(buff);
        auto msg = std::get_if<incomplete_t>(&r);
        REQUIRE(msg);
        serialize(ping_t{}, buff);
        r = parse(std::string_view(buff.data(), buff.size() - 1));
        msg = std::get_if<incomplete_t>(&r);
        REQUIRE(msg);
    }

    SECTION("protocol errors") {
        SECTION("wrong magic") {
            auto sz = serialize(ping_t{}, buff);
            REQUIRE(sz);
            buff[0] = -1;
            auto r = parse(buff);
            CHECK(std::get_if<protocol_error_t>(&r));
        }
        SECTION("wrong size") {
            auto sz = serialize(ping_t{}, buff);
            REQUIRE(sz);
            buff[4] = -1;
            auto r = parse(buff);
            CHECK(std::get_if<protocol_error_t>(&r));
        }
        SECTION("wrong type") {
            auto sz = serialize(ping_t{}, buff);
            REQUIRE(sz);
            buff[9] = -1;
            auto r = parse(buff);
            CHECK(std::get_if<protocol_error_t>(&r));
        }
    }
}

TEST_CASE("endpoing parsing", "[relay]") {
    std::string body = R""(
{
  "relays": [
    {
      "url": "relay://130.61.176.206:22067/?id=OAKAXEX-7HE764M-5EWVN7U-SZCQU4D-ZPXF2TY-SNTL2LL-Y5RVGVM-U7WBRA3&pingInterval=1m30s&networkTimeout=2m0s&sessionLimitBps=0&globalLimitBps=0&statusAddr=:22070&providedBy=ina",
      "location": {
        "latitude": 50.1049,
        "longitude": 8.6295,
        "city": "Frankfurt am Main",
        "country": "DE",
        "continent": "EU"
      },
      "stats": {
        "startTime": "2022-04-16T09:12:21.941097618Z",
        "uptimeSeconds": 1312802,
        "numPendingSessionKeys": 0,
        "numActiveSessions": 46,
        "numConnections": 494,
        "numProxies": 88,
        "bytesProxied": 493207937616,
        "goVersion": "go1.16.3",
        "goOS": "linux",
        "goArch": "arm64",
        "goMaxProcs": 4,
        "goNumRoutine": 1136,
        "kbps10s1m5m15m30m60m": [
          241,
          284,
          309,
          332,
          355,
          312
        ],
        "options": {
          "network-timeout": 120,
          "ping-interval": 60,
          "message-timeout": 60,
          "per-session-rate": 0,
          "global-rate": 0,
          "pools": [
            "https://relays.syncthing.net/endpoint"
          ],
          "provided-by": "ina"
        }
      },
      "statsRetrieved": "2022-05-01T13:52:24.524417759Z"
    }
]
}
    )"";
    auto r = parse_endpoint(body);
    REQUIRE(r);
    REQUIRE(r.value().size() == 1);
    auto relay = r.value()[0];
    CHECK(relay->uri->buffer() ==
          "relay://130.61.176.206:22067/"
          "?id=OAKAXEX-7HE764M-5EWVN7U-SZCQU4D-ZPXF2TY-SNTL2LL-Y5RVGVM-U7WBRA3&pingInterval=1m30s&"
          "networkTimeout=2m0s&sessionLimitBps=0&globalLimitBps=0&statusAddr=:22070&providedBy=ina");
    CHECK(relay->device_id.get_short() == "OAKAXEX");
    auto &l = relay->location;
    CHECK(abs(l.latitude - 50.1049) < 0.0001);
    CHECK(abs(l.longitude - 8.6295) < 0.0001);
    CHECK(l.city == "Frankfurt am Main");
    CHECK(l.country == "DE");
    CHECK(l.continent == "EU");
    CHECK(relay->ping_interval == pt::seconds{90});

    auto device = parse_device(relay->uri);
    REQUIRE(device);
    CHECK(device.value() == relay->device_id);
}
