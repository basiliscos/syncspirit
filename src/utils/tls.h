#pragma once
#include <boost/outcome.hpp>
#include <memory>
#include <openssl/x509v3.h>
#include <openssl/evp.h>

namespace syncspirit {
namespace utils {

namespace outcome = boost::outcome_v2;

template <typename T> using guard_t = std::unique_ptr<T, std::function<void(T *)>>;

struct key_pair_t {
    using X509_sp = guard_t<X509>;
    using EVP_PKEY_sp = guard_t<EVP_PKEY>;

    X509_sp cert;
    EVP_PKEY_sp private_key;
};

outcome::result<key_pair_t> generate_pair(const char *issuer_name) noexcept;

} // namespace utils
} // namespace syncspirit
