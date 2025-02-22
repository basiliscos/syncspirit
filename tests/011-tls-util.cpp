// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "test-utils.h"
#include "utils/base32.h"
#include "utils/tls.h"
#include <openssl/pem.h>
#include <filesystem>
#include <cstdio>

using namespace syncspirit::utils;
using namespace syncspirit::test;

namespace bfs = std::filesystem;

TEST_CASE("generate cert/key pair, save & load", "[support][tls]") {
    auto pair = generate_pair("sample");
    REQUIRE((bool)pair);
    REQUIRE((bool)pair.value().cert);
    REQUIRE((bool)pair.value().private_key);
    REQUIRE(pair.value().cert_data.bytes.size() > 0);

    auto &value = pair.value();

    PEM_write_PrivateKey(stdout, value.private_key.get(), nullptr, nullptr, 0, nullptr, nullptr);
    PEM_write_X509(stdout, value.cert.get());
    X509_print_fp(stdout, value.cert.get());

    auto cert_file = unique_path();
    auto cert_file_path = cert_file.string();
    auto cert_file_guard = path_guard_t(cert_file);

    auto key_file = unique_path();
    auto key_file_path = key_file.string();
    auto key_file_guard = path_guard_t(key_file);
    auto save_result = value.save(cert_file_path.c_str(), key_file_path.c_str());
    REQUIRE((bool)save_result);
    printf("cert has been saved as %s\n", cert_file_path.c_str());

    auto load_result = load_pair(cert_file_path.c_str(), key_file_path.c_str());
    REQUIRE((bool)load_result);
    REQUIRE(load_result.value().cert_data.bytes.size() == pair.value().cert_data.bytes.size());

    bool bytes_equal = load_result.value().cert_data.bytes == pair.value().cert_data.bytes;
    REQUIRE(bytes_equal);

    auto cn = get_common_name(value.cert.get());
    REQUIRE(cn);
    REQUIRE(cn.value() == "sample");
}

TEST_CASE("sha256 for certificate", "[support][tls]") {
    auto cert = read_file(locate_path("data/cert.der"));
    auto sha_result = sha256_digest(cert);
    REQUIRE((bool)sha_result);
    auto &sha = sha_result.value();
    REQUIRE(1 == 1);
    std::string expected = "b1b48b580b78b47c975a138b4aaa2988fc621795c95a2868e24d93b327e8858c";
    std::string got_str;

    for (std::size_t i = 0; i < sha.size(); i++) {
        got_str += fmt::format("{:02x}", (unsigned char)sha[i]);
    }
    REQUIRE(got_str == expected);

    auto enc = base32::encode(sha);
    REQUIRE(enc == "WG2IWWALPC2HZF22COFUVKRJRD6GEF4VZFNCQ2HCJWJ3GJ7IQWGA");
}
