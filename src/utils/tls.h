#pragma once
#include <string>
#include <memory>
#include <vector>
#include <boost/outcome.hpp>
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
    std::string cert_data; /* in DER-format */

    outcome::result<void> save(const char *cert, const char *priv_key) const noexcept;
};

outcome::result<key_pair_t> generate_pair(const char *issuer_name) noexcept;

outcome::result<key_pair_t> load_pair(const char *cert, const char *priv_key) noexcept;

outcome::result<std::string> sha256_digest(const std::string &data) noexcept;

} // namespace utils
} // namespace syncspirit
