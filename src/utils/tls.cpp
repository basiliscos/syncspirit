// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "tls.h"
#include "error_code.h"
#include "io.h"
#include <random>
#include <boost/system/error_code.hpp>
#include <openssl/pem.h>
#include <openssl/sha.h>
#include <openssl/err.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/bio.h>

#ifdef OSSL_DEPRECATEDIN_3_0
#include <openssl/encoder.h>
#endif

namespace sys = boost::system;

namespace syncspirit::utils {


bool cert_data_t::operator==(const cert_data_t& other) const noexcept {
    auto view_mine = bytes_view_t(*this);
    auto view_other = bytes_view_t(other);
    return view_mine == view_other;
}

x509_t::~x509_t() {
    if (cert) {
        X509_free(cert);
    }
}

template <typename T, typename G> guard_t<T> make_guard(T *ptr, G &&fn) {
    return guard_t<T>{ptr, [fn = std::move(fn)](T *it) { fn(it); }};
}

static bool add_extension(X509V3_CTX &ctx, X509 *cert, int NID_EXT, const char *value) {
    auto ex = X509V3_EXT_conf_nid(nullptr, &ctx, NID_EXT, value);
    if (!ex) {
        return false;
    }
    auto ex_guard = make_guard(ex, [](auto *ptr) { X509_EXTENSION_free(ptr); });
    if (1 != X509_add_ext(cert, ex, -1)) {
        return false;
    }
    return true;
}

template <typename Key, typename Fn> static outcome::result<utils::bytes_t> as_der_impl(Key *cert, Fn &&fn) noexcept {
    BIO *bio = BIO_new(BIO_s_mem());
    auto bio_guard = make_guard(bio, [](auto *ptr) { BIO_free(ptr); });
    if (fn(bio, cert) < 0) {
        return error_code_t::tls_cert_save_failure;
    }
    char *cert_buff;
    auto cert_sz = BIO_get_mem_data(bio, &cert_buff);
    if (cert_sz < 0) {
        return error_code_t::tls_cert_save_failure;
    }
    auto cert_container = utils::bytes_t(static_cast<std::size_t>(cert_sz));
    std::memcpy(cert_container.data(), cert_buff, cert_container.size());
    return cert_container;
}

static outcome::result<utils::bytes_t> as_der(X509 *cert) noexcept {
    return as_der_impl(cert, [](BIO *bio, auto *cert) { return i2d_X509_bio(bio, cert); });
}

static outcome::result<utils::bytes_t> as_der(EVP_PKEY *key) noexcept {
    auto pkc8 = EVP_PKEY2PKCS8(key);
    auto pkc8_guard = make_guard(pkc8, [](auto *ptr) { PKCS8_PRIV_KEY_INFO_free(ptr); });
    return as_der_impl(pkc8, [](BIO *bio, auto *key) { return i2d_PKCS8_PRIV_KEY_INFO_bio(bio, key); });
}

outcome::result<utils::bytes_t> as_serialized_der(X509 *cert) noexcept { return as_der(cert); }

outcome::result<key_pair_t> generate_pair(const char *issuer_name) noexcept {
    auto ev_ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr);
    if (ev_ctx == nullptr) {
        return error_code_t::tls_context_init_failure;
    }
    auto ev_ctx_guard = make_guard(ev_ctx, [](auto *ptr) { EVP_PKEY_CTX_free(ptr); });

    if (1 != EVP_PKEY_paramgen_init(ev_ctx)) {
        return error_code_t::tls_param_init_failure;
    }

    if (1 != EVP_PKEY_CTX_set_ec_paramgen_curve_nid(ev_ctx, NID_secp384r1)) {
        return error_code_t::tls_ec_curve_failure;
    }

    EVP_PKEY *params = nullptr;
    if (!EVP_PKEY_paramgen(ev_ctx, &params)) {
        return error_code_t::tls_param_gen_failure;
    }
    auto params_guard = make_guard(params, [](auto *ptr) { EVP_PKEY_free(ptr); });

    EVP_PKEY_CTX *key_ctx = EVP_PKEY_CTX_new(params, nullptr);
    auto key_ctx_guard = make_guard(key_ctx, [](auto *ptr) { EVP_PKEY_CTX_free(ptr); });
    /* Generate the key */
    if (1 != EVP_PKEY_keygen_init(key_ctx)) {
        return error_code_t::tls_key_gen_init_failure;
    }

    EVP_PKEY *pkey = nullptr;
    if (1 != EVP_PKEY_keygen(key_ctx, &pkey)) {
        return error_code_t::tls_key_gen_failure;
    }
    auto pkey_guard = make_guard(pkey, [](auto *ptr) { EVP_PKEY_free(ptr); });

    EC_GROUP *group = EC_GROUP_new_by_curve_name(NID_secp384r1);
    EC_GROUP_set_asn1_flag(group, OPENSSL_EC_NAMED_CURVE);
    if (group == nullptr) {
        return error_code_t::tls_ec_group_failure;
    }
    auto group_guard = make_guard(group, [](auto *ptr) { EC_GROUP_free(ptr); });

    X509 *cert = X509_new();
    // cert->ex_kusage = X509v3_KU_KEY_ENCIPHERMENT | X509v3_KU_DIGITAL_SIGNATURE;
    auto cert_guard = make_guard(cert, [](auto *ptr) { X509_free(ptr); });

    std::random_device rd;
    std::mt19937 generator(rd());
    constexpr const auto max_sn = std::numeric_limits<std::uint64_t>::max() >> 1;
    std::uniform_int_distribution<std::uint64_t> distr(1, max_sn);
    int version = 2;
    long serial = static_cast<long>(distr(generator));
    long start_epoch = 0; /* now */
    long end_epoch = 2524568399;

    if (1 != X509_set_version(cert, version)) {
        return error_code_t::tls_cert_set_failure;
    }

    if (-1 == ASN1_INTEGER_set(X509_get_serialNumber(cert), serial)) {
        return error_code_t::tls_cert_set_failure;
    }

    X509_gmtime_adj(X509_get_notBefore(cert), start_epoch);
    X509_gmtime_adj(X509_get_notAfter(cert), end_epoch);

    if (1 != X509_set_pubkey(cert, pkey)) {
        return error_code_t::tls_cert_set_failure;
    }

    auto name = X509_get_subject_name(cert);
    if (1 != X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, reinterpret_cast<const unsigned char *>(issuer_name),
                                        -1, -1, 0)) {
        return error_code_t::tls_cert_set_failure;
    }

    if (1 != X509_set_issuer_name(cert, name)) {
        return error_code_t::tls_cert_set_failure;
    }

    X509V3_CTX ctx;
    ctx.db = nullptr; /* no DB is used */
    X509V3_set_ctx(&ctx, cert, cert, nullptr, nullptr, 0);

    if (!add_extension(ctx, cert, NID_basic_constraints, "critical, CA:false")) {
        return error_code_t::tls_cert_ext_failure;
    }

    if (!add_extension(ctx, cert, NID_key_usage, "critical, Digital Signature, Key Encipherment")) {
        return error_code_t::tls_cert_ext_failure;
    }

    if (!add_extension(ctx, cert, NID_ext_key_usage, "TLS Web Server Authentication, TLS Web Client Authentication")) {
        return error_code_t::tls_cert_ext_failure;
    }

    auto bytes = X509_sign(cert, pkey, EVP_sha256());
    if (!bytes) {
        return error_code_t::tls_cert_sign_failure;
    }

    auto cert_container = as_der(cert);
    if (!cert_container) {
        return cert_container.error();
    }

    auto key_container = as_der(pkey);
    if (!key_container) {
        return key_container.error();
    }

    return key_pair_t{std::move(cert_guard), std::move(pkey_guard), cert_data_t{std::move(cert_container.value())},
                      cert_data_t{std::move(key_container.value())}};
}

static bool write_mem_to(const char *path, BIO *mem) {
    auto file = ofstream_t(path, ifstream_t::out | ifstream_t::binary);
    char *ptr;
    auto size = BIO_get_mem_data(mem, &ptr);
    if (size < 0) {
        return false;
    }
    if (size == 0) {
        return true;
    }
    file.write(ptr, size);
    return (bool)file;
}

outcome::result<void> key_pair_t::save(const char *cert_path, const char *priv_key_path) const noexcept {
    do {
        BIO *bio = BIO_new(BIO_s_mem());
        auto bio_guard = make_guard(bio, [](auto ptr) { BIO_free(ptr); });
        if (1 != PEM_write_bio_X509(bio, cert.get())) {
            return error_code_t::tls_cert_save_failure;
        }
        if (!write_mem_to(cert_path, bio)) {
            return error_code_t::tls_cert_save_failure;
        }
    } while (0);

    do {
        BIO *bio = BIO_new(BIO_s_mem());
        auto bio_guard = make_guard(bio, [](auto ptr) { BIO_free(ptr); });
        if (1 != PEM_write_bio_PrivateKey(bio, private_key.get(), nullptr, nullptr, 0, nullptr, nullptr)) {
            return error_code_t::tls_key_save_failure;
        }
        if (!write_mem_to(priv_key_path, bio)) {
            return error_code_t::tls_key_save_failure;
        }
    } while (0);

    return outcome::success();
}

static outcome::result<guard_t<BIO>> read_to_mem_bio(const char *cert_path) {
    auto file = ifstream_t(cert_path, ifstream_t::in | ifstream_t::binary);
    if (!file) {
        return sys::error_code{errno, sys::system_category()};
    }

    auto begin = file.tellg();
    if (!file.seekg(0, ifstream_t::end)) {
        return sys::error_code{errno, sys::generic_category()};
    }
    auto end = file.tellg();
    if (end < 0) {
        return sys::error_code{errno, sys::generic_category()};
    }
    auto cert_sz = end - begin;
    if (!file.seekg(ifstream_t::beg)) {
        return sys::error_code{errno, sys::generic_category()};
    }
    auto data = std::vector<char>(cert_sz);
    auto ptr = data.data();
    file.read(ptr, cert_sz);

    auto cert_bio = BIO_new_mem_buf(ptr, static_cast<int>(cert_sz));
    return make_guard(cert_bio, [data = std::move(data)](auto *ptr) { BIO_free(ptr); });
}

outcome::result<key_pair_t> load_pair(const char *cert_path, const char *priv_key_path) {
    /* read certificate in memory, then load it va openssl */
    auto cert_mem_result = read_to_mem_bio(cert_path);
    if (!cert_mem_result) {
        return cert_mem_result.assume_error();
    }
    auto cert_bio = cert_mem_result.assume_value().get();
    X509 *cert = X509_new();
    auto cert_guard = make_guard(cert, [](auto *ptr) { X509_free(ptr); });
    if (!PEM_read_bio_X509(cert_bio, &cert, nullptr, nullptr)) {
        return error_code_t::tls_cert_load_failure;
    }

    /* read private key */
    auto pk_mem_result = read_to_mem_bio(priv_key_path);
    if (!pk_mem_result) {
        return pk_mem_result.assume_error();
    }
    auto pk_bio = pk_mem_result.assume_value().get();
    auto pkey = PEM_read_bio_PrivateKey(pk_bio, nullptr, nullptr, nullptr);
    if (!pkey) {
        return error_code_t::tls_key_load_failure;
    }
    auto pkey_guard = make_guard(pkey, [](auto *ptr) { EVP_PKEY_free(ptr); });

    auto cert_container = as_der(cert);
    if (!cert_container) {
        return cert_container.error();
    }

    auto key_container = as_der(pkey);
    if (!key_container) {
        return key_container.error();
    }

    return key_pair_t{std::move(cert_guard), std::move(pkey_guard), cert_data_t{std::move(cert_container.value())},
                      cert_data_t{std::move(key_container.value())}};
}

outcome::result<bytes_t> sha256_digest(utils::bytes_view_t data) noexcept {
    unsigned char buff[SHA256_DIGEST_LENGTH];
    auto ctx = EVP_MD_CTX_create();
    auto ctx_guard = make_guard(ctx, [](auto *ptr) { EVP_MD_CTX_free(ptr); });

    auto digester = EVP_get_digestbyname("sha256");
    if (!digester) {
        return error_code_t::tls_sha256_init_failure;
    }
    if (1 != EVP_DigestInit_ex(ctx, digester, NULL)) {
        return error_code_t::tls_sha256_failure;
    }
    if (1 != EVP_DigestUpdate(ctx, data.data(), data.size())) {
        return error_code_t::tls_sha256_failure;
    }
    EVP_DigestFinal_ex(ctx, buff, NULL);
    return {buff, buff + SHA256_DIGEST_LENGTH};
}

outcome::result<std::string> get_common_name(X509 *cert) noexcept {
    auto name = X509_get_subject_name(cert);

    char buff[256] = {};
    int r = X509_NAME_get_text_by_NID(name, NID_commonName, buff, 256);
    if (r == -1) {
        return error_code_t::tls_cn_missing;
    }
    return std::string(buff, r);
}

void digest(const char *src, size_t length, char *storage) noexcept {
    SHA256((const unsigned char *)src, length, (unsigned char *)storage);
}

} // namespace syncspirit::utils
