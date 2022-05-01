// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2022 Ivan Baidakou

#include "catch.hpp"
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
            auto source = session_invitation_t{"lorem", "impsum", "dolor", 1234, true};
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
            CHECK(target->address == source.address);
            CHECK(target->server_socket == source.server_socket);
        }
    }

    SECTION("incompplete") {
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
      "url": "relay://130.61.176.206:22067/?id=OAKAXEX-7HE764M-5EWVN7U-SZCQU4D-ZPXF2TY-SNTL2LL-Y5RVGVM-U7WBRA3&pingInterval=1m0s&networkTimeout=2m0s&sessionLimitBps=0&globalLimitBps=0&statusAddr=:22070&providedBy=ina",
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
    CHECK(relay->uri.full == "relay://130.61.176.206:22067/"
                             "?id=OAKAXEX-7HE764M-5EWVN7U-SZCQU4D-ZPXF2TY-SNTL2LL-Y5RVGVM-U7WBRA3&pingInterval=1m0s&"
                             "networkTimeout=2m0s&sessionLimitBps=0&globalLimitBps=0&statusAddr=:22070&providedBy=ina");
    auto &l = relay->location;
    CHECK(abs(l.latitude - 50.1049) < 0.0001);
    CHECK(abs(l.longitude - 8.6295) < 0.0001);
    CHECK(l.city == "Frankfurt am Main");
    CHECK(l.country == "DE");
    CHECK(l.continent == "EU");
}
