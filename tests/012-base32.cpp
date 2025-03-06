// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "test-utils.h"
#include "utils/base32.h"

using namespace syncspirit::test;
using namespace syncspirit::utils;

namespace fs = std::filesystem;
using b32 = syncspirit::utils::base32;

TEST_CASE("base32 encode/decode", "[support]") {

    SECTION("encode") {
        REQUIRE(b32::encode(bytes_view_t((const unsigned char *)"f", 1)) == "MY");
        REQUIRE(b32::encode(bytes_view_t((const unsigned char *)"fo", 2)) == "MZXQ");
        REQUIRE(b32::encode(bytes_view_t((const unsigned char *)"foo", 3)) == "MZXW6");
        REQUIRE(b32::encode(bytes_view_t((const unsigned char *)"foo-", 4)) == "MZXW6LI");
        REQUIRE(b32::encode(bytes_view_t((const unsigned char *)"foo-b", 5)) == "MZXW6LLC");
        REQUIRE(b32::encode(bytes_view_t((const unsigned char *)"foo-bar", 7)) == "MZXW6LLCMFZA");
    }

    SECTION("decode") {
        REQUIRE(b32::decode("MY").value() == bytes_view_t((const unsigned char *)"f", 1));
        REQUIRE(b32::decode("MZXQ").value() == bytes_view_t((const unsigned char *)"fo", 2));
        REQUIRE(b32::decode("MZXW6").value() == bytes_view_t((const unsigned char *)"foo", 3));
        REQUIRE(b32::decode("MZXW6LI").value() == bytes_view_t((const unsigned char *)"foo-", 4));
        REQUIRE(b32::decode("MZXW6LLC").value() == bytes_view_t((const unsigned char *)"foo-b", 5));
        REQUIRE(b32::decode("MZXW6LLCMFZA").value() == bytes_view_t((const unsigned char *)"foo-bar", 7));
    }

    SECTION("encode-decode lorum") {
        std::string_view in = "lorem ipsum dolor sit amet";
        auto bytes_in = bytes_view_t((const unsigned char *)in.data(), in.size());
        auto text_out = b32::encode(bytes_in);
        REQUIRE(text_out == "NRXXEZLNEBUXA43VNUQGI33MN5ZCA43JOQQGC3LFOQ");
        REQUIRE(b32::decode(text_out).value() == bytes_in);
    }

    SECTION("binary string") {
        unsigned char in[] = {0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6, 0x00};
        auto bytes_in = bytes_view_t(in, sizeof(in));
        auto e = b32::encode(bytes_in);
        auto d = b32::decode(e);
        REQUIRE((bool)d);
        REQUIRE(d.value() == bytes_in);
    }
}
