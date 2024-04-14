// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2023 Ivan Baidakou

#include "test-utils.h"
#include "utils/base32.h"

using namespace syncspirit::test;

namespace fs = boost::filesystem;
using b32 = syncspirit::utils::base32;

TEST_CASE("base32 encode/decode", "[support]") {

    SECTION("encode") {
        REQUIRE(b32::encode("f") == "MY");
        REQUIRE(b32::encode("fo") == "MZXQ");
        REQUIRE(b32::encode("foo") == "MZXW6");
        REQUIRE(b32::encode("foo-") == "MZXW6LI");
        REQUIRE(b32::encode("foo-b") == "MZXW6LLC");
        REQUIRE(b32::encode("foo-bar") == "MZXW6LLCMFZA");
        REQUIRE(b32::encode("lorem ipsum dolor sit amet") == "NRXXEZLNEBUXA43VNUQGI33MN5ZCA43JOQQGC3LFOQ");
    }

    SECTION("decode") {
        REQUIRE(b32::decode("MY").value() == "f");
        REQUIRE(b32::decode("MZXQ").value() == "fo");
        REQUIRE(b32::decode("MZXW6").value() == "foo");
        REQUIRE(b32::decode("MZXW6LI").value() == "foo-");
        REQUIRE(b32::decode("MZXW6LLC").value() == "foo-b");
        REQUIRE(b32::decode("MZXW6LLCMFZA").value() == "foo-bar");
        REQUIRE(b32::decode("NRXXEZLNEBUXA43VNUQGI33MN5ZCA43JOQQGC3LFOQ").value() == "lorem ipsum dolor sit amet");
    }

    SECTION("binary string") {
        unsigned char in[] = {0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6, 0x00};
        std::string orig(reinterpret_cast<const char *>(in));
        auto e = b32::encode(orig);
        auto d = b32::decode(e);
        REQUIRE((bool)d);
        REQUIRE(d.value() == orig);
    }
}
