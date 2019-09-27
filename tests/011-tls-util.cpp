#include "catch.hpp"
#include "utils/tls.h"
#include <openssl/pem.h>

using namespace syncspirit::utils;

TEST_CASE("generate cert/key pair", "[support][tls]") {
    auto pair = generate_pair("sample");
    REQUIRE((bool) pair);
    REQUIRE((bool) pair.value().cert);
    REQUIRE((bool) pair.value().private_key);

    auto& value = pair.value();

    PEM_write_PrivateKey(stdout, value.private_key.get(), nullptr, nullptr, 0, nullptr, nullptr);
    PEM_write_X509(stdout, value.cert.get());
    X509_print_fp(stdout, value.cert.get());
}
