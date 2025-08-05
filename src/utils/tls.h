// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include <memory>
#include <string>
#include <boost/outcome.hpp>
#include <openssl/x509v3.h>
#include <openssl/evp.h>
#include "syncspirit-export.h"
#include "bytes.h"

namespace syncspirit {
namespace utils {

namespace outcome = boost::outcome_v2;

template <typename T> using guard_t = std::unique_ptr<T, std::function<void(T *)>>;

/* in DER-format */
struct cert_data_view_t : bytes_view_t {
    using bytes_view_t::bytes_view_t;
};
struct SYNCSPIRIT_API cert_data_t : bytes_t {
    inline explicit cert_data_t(bytes_t bytes) noexcept : bytes_t(std::move(bytes)) {}
    using bytes_t::bytes_t;
    bool operator==(const cert_data_t &other) const noexcept;
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

SYNCSPIRIT_API outcome::result<bytes_t> sha256_digest(utils::bytes_view_t data) noexcept;

SYNCSPIRIT_API outcome::result<bytes_t> as_serialized_der(X509 *cert) noexcept;

SYNCSPIRIT_API outcome::result<std::string> get_common_name(X509 *cert) noexcept;

SYNCSPIRIT_API void digest(const unsigned char *src, size_t length, unsigned char *storage) noexcept;

} // namespace utils
} // namespace syncspirit
