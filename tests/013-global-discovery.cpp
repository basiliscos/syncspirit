// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "test-utils.h"
#include "proto/discovery_support.h"
#include "utils/error_code.h"
#include <boost/filesystem.hpp>

namespace sys = boost::system;
namespace fs = boost::filesystem;

using namespace syncspirit::proto;
using namespace syncspirit::utils;
using namespace syncspirit::test;

TEST_CASE("parse valid announce sample 1", "[support]") {
    std::string body =
        R""(
                {"seen":"2020-10-13T18:41:37.02287354Z",
                "addresses":["quic://192.168.100.15:22000",
                "quic://81.52.93.44:22000",
                "quic://81.52.93.44:22000",
                "relay://188.68.32.45:22067/?id=O4LHPKG-O6BQ36W-MUOVKTI-MKAVHSC-Y7EC3U4-DHNLEDE-MZBJWQN-UIX6QAL&pingInterval=1m0s&networkTimeout=2m0s&sessionLimitBps=0&globalLimitBps=0&statusAddr=22070",
                "tcp://192.168.100.15:22000",
                "tcp://81.52.93.44:22000",
                "tcp://81.52.93.44:22000",
                "tcp://81.52.93.44:55386",
                "tcp://81.52.93.44:55386"]}
            )"";

    http::response<http::string_body> res;
    res.result(200);
    res.body() = body;
    auto r = parse_contact(res);
    REQUIRE((bool)r);

    auto &o = r.value();
    CHECK(o.size() == 9);
    CHECK(o[0]->buffer() == "quic://192.168.100.15:22000");
}

TEST_CASE("parse valid announce sample 2", "[support]") {
    std::string body =
        R""(
{
  "seen": "2023-10-31T09:38:31Z",
  "addresses": [
	"relay://93.31.21.95:443/?id=QUJWY5Q-YUKKWZF-OWOJ66C-NWAPXYC-LWU4IPC-TO6UBKB-67R5JV5-BILNNA7"
  ]
}
			)"";

    http::response<http::string_body> res;
    res.result(200);
    res.body() = body;
    auto r = parse_contact(res);
    REQUIRE((bool)r);

    auto &o = r.value();
    CHECK(o.size() == 1);
    CHECK(o[0]->buffer() ==
          "relay://93.31.21.95:443/?id=QUJWY5Q-YUKKWZF-OWOJ66C-NWAPXYC-LWU4IPC-TO6UBKB-67R5JV5-BILNNA7");
}

TEST_CASE("malformed url", "[support]") {

    http::response<http::string_body> res;
    res.result(200);

    SECTION("malformed url") {
        res.body() = R""( {"seen":"2020-10-13T18:41:37.02287354Z", "addresses":["q uic://192.168.100.15:22000" ]})"";
        auto r = parse_contact(res);
        CHECK(r.error() == make_error_code(error_code_t::malformed_url));
    }

    SECTION("malformed json") {
        res.body() = R""( {"seen":"2020-10-13T18:41:37.02287354Z" "addresses":["qic://192.168.100.15:22000" ]})"";
        auto r = parse_contact(res);
        CHECK(r.error() == make_error_code(error_code_t::malformed_json));
    }

    SECTION("incorrect json") {
        res.body() = R""( {"seen":"2020-10-13T18:41:37.02287354Z", "Addresses":["quic://192.168.100.15:22000" ]})"";
        auto r = parse_contact(res);
        CHECK(r.error() == make_error_code(error_code_t::incorrect_json));
    }

    SECTION("malformed date") {
        res.body() = R""( {"seen":"2020-99-13T18:41:37.02287354Z", "addresses":["qic://192.168.100.15:22000" ]})"";
        auto r = parse_contact(res);
        CHECK(r.error() == make_error_code(error_code_t::malformed_date));
    }

    SECTION("wrong code") {
        res.result(500);
        auto r = parse_contact(res);
        CHECK(r.error() == make_error_code(error_code_t::unexpected_response_code));
    }

    SECTION("not found") {
        res.result(404);
        auto r = parse_contact(res);
        REQUIRE((bool)r);
        CHECK(r.value().empty());
    }
}
