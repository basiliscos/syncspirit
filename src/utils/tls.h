// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#pragma once

#include <string>
#include <memory>
#include <vector>
#include <boost/outcome.hpp>
#include <openssl/x509v3.h>
#include <openssl/evp.h>
#include "syncspirit-export.h"

namespace syncspirit {
namespace utils {

namespace outcome = boost::outcome_v2;

template <typename T> using guard_t = std::unique_ptr<T, std::function<void(T *)>>;

struct cert_data_t {
    std::string bytes; /* in DER-format */
};

struct SYNCSPIRIT_API key_pair_t {
    using X509_sp = guard_t<X509>;
    using EVP_PKEY_sp = guard_t<EVP_PKEY>;

    X509_sp cert;
    EVP_PKEY_sp private_key;
    cert_data_t cert_data;
    cert_data_t key_data;

    outcome::result<void> save(const char *cert, const char *priv_key) const noexcept;
};

struct SYNCSPIRIT_API x509_t {
    x509_t() noexcept : cert{nullptr} {}
    x509_t(X509 *cert_) noexcept : cert{cert_} {}
    ~x509_t();

    inline operator X509 *() noexcept { return cert; }

    X509 *cert;
};

SYNCSPIRIT_API outcome::result<key_pair_t> generate_pair(const char *issuer_name) noexcept;

SYNCSPIRIT_API outcome::result<key_pair_t> load_pair(const char *cert, const char *priv_key);

SYNCSPIRIT_API outcome::result<std::string> sha256_digest(const std::string &data) noexcept;

SYNCSPIRIT_API outcome::result<std::string> as_serialized_der(X509 *cert) noexcept;

SYNCSPIRIT_API outcome::result<std::string> get_common_name(X509 *cert) noexcept;

SYNCSPIRIT_API void digest(const char *src, size_t length, char *storage) noexcept;

} // namespace utils
} // namespace syncspirit
