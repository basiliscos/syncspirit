#include "tls.h"
#include "error_code.h"
#include <random>
#include <cstdio>
#include <boost/system/error_code.hpp>
#include <openssl/pem.h>
#include <openssl/sha.h>

namespace sys = boost::system;

namespace syncspirit::utils {

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

template <typename Key, typename Fn> static outcome::result<std::string> as_der_impl(Key *cert, Fn &&fn) noexcept {
    BIO *bio = BIO_new(BIO_s_mem());
    auto bio_guard = make_guard(bio, [](auto *ptr) { BIO_free(ptr); });
    if (fn(bio, cert) < 0) {
        return error_code::tls_cert_save_failure;
    }
    char *cert_buff;
    auto cert_sz = BIO_get_mem_data(bio, &cert_buff);
    if (cert_sz < 0) {
        return error_code::tls_cert_save_failure;
    }
    std::string cert_container(static_cast<std::size_t>(cert_sz), 0);
    std::memcpy(cert_container.data(), cert_buff, cert_container.size());
    return cert_container;
}

static outcome::result<std::string> as_der(X509 *cert) noexcept {
    return as_der_impl(cert, [](BIO *bio, auto *cert) { return i2d_X509_bio(bio, cert); });
}

static outcome::result<std::string> as_der(EVP_PKEY *key) noexcept {
    return as_der_impl(key, [](BIO *bio, auto *key) { return i2d_PUBKEY_bio(bio, key); });
}

#if 0
static outcome::result<std::string> as_der(X509 *cert) noexcept {
    BIO *bio = BIO_new(BIO_s_mem());
    auto bio_guard = make_guard(bio, [](auto *ptr) { BIO_free(ptr); });
    if (i2d_X509_bio(bio, cert) < 0) {
        return error_code::tls_cert_save_failure;
    }
    char *cert_buff;
    auto cert_sz = BIO_get_mem_data(bio, &cert_buff);
    if (cert_sz < 0) {
        return error_code::tls_cert_save_failure;
    }
    std::string cert_container(static_cast<std::size_t>(cert_sz), 0);
    std::memcpy(cert_container.data(), cert_buff, cert_container.size());
    return cert_container;
}

static outcome::result<std::string> as_der(EVP_PKEY *key) noexcept {
    BIO *bio = BIO_new(BIO_s_mem());
    auto bio_guard = make_guard(bio, [](auto *ptr) { BIO_free(ptr); });
    if (i2d_PUBKEY_bio(bio, key) < 0) {
        return error_code::tls_cert_save_failure;
    }
    char *cert_buff;
    auto cert_sz = BIO_get_mem_data(bio, &cert_buff);
    if (cert_sz < 0) {
        return error_code::tls_cert_save_failure;
    }
    std::string cert_container(static_cast<std::size_t>(cert_sz), 0);
    std::memcpy(cert_container.data(), cert_buff, cert_container.size());
    return cert_container;
}
#endif

outcome::result<key_pair_t> generate_pair(const char *issuer_name) noexcept {
    auto ev_ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr);
    if (ev_ctx == nullptr) {
        return error_code::tls_context_init_failure;
    }
    auto ev_ctx_guard = make_guard(ev_ctx, [](auto *ptr) { EVP_PKEY_CTX_free(ptr); });

    if (1 != EVP_PKEY_paramgen_init(ev_ctx)) {
        return error_code::tls_param_init_failure;
    }

    if (1 != EVP_PKEY_CTX_set_ec_paramgen_curve_nid(ev_ctx, NID_secp384r1)) {
        return error_code::tls_ec_curve_failure;
    }

    EVP_PKEY *params = nullptr;
    if (!EVP_PKEY_paramgen(ev_ctx, &params)) {
        return error_code::tls_param_gen_failure;
    }
    auto params_guard = make_guard(params, [](auto *ptr) { EVP_PKEY_free(ptr); });

    EVP_PKEY_CTX *key_ctx = EVP_PKEY_CTX_new(params, nullptr);
    auto key_ctx_guard = make_guard(key_ctx, [](auto *ptr) { EVP_PKEY_CTX_free(ptr); });
    /* Generate the key */
    if (1 != EVP_PKEY_keygen_init(key_ctx)) {
        return error_code::tls_key_gen_init_failure;
    }

    EVP_PKEY *pkey = nullptr;
    if (1 != EVP_PKEY_keygen(key_ctx, &pkey)) {
        return error_code::tls_key_gen_failure;
    }
    auto pkey_quard = make_guard(pkey, [](auto *ptr) { EVP_PKEY_free(ptr); });

    EC_GROUP *group = EC_GROUP_new_by_curve_name(NID_secp384r1);
    EC_GROUP_set_asn1_flag(group, OPENSSL_EC_NAMED_CURVE);
    if (group == nullptr) {
        return error_code::tls_ec_group_failure;
    }
    auto group_guard = make_guard(group, [](auto *ptr) { EC_GROUP_free(ptr); });

    auto ec_key = EVP_PKEY_get0_EC_KEY(pkey);
    EC_KEY_set_group(ec_key, group);

    X509 *cert = X509_new();
    cert->ex_kusage = X509v3_KU_KEY_ENCIPHERMENT | X509v3_KU_DIGITAL_SIGNATURE;
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
        return error_code::tls_cert_set_failure;
    }

    if (-1 == ASN1_INTEGER_set(X509_get_serialNumber(cert), serial)) {
        return error_code::tls_cert_set_failure;
    }

    X509_gmtime_adj(X509_get_notBefore(cert), start_epoch);
    X509_gmtime_adj(X509_get_notAfter(cert), end_epoch);

    if (1 != X509_set_pubkey(cert, pkey)) {
        return error_code::tls_cert_set_failure;
    }

    auto name = X509_get_subject_name(cert);
    if (1 != X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, reinterpret_cast<const unsigned char *>(issuer_name),
                                        -1, -1, 0)) {
        return error_code::tls_cert_set_failure;
    }

    if (1 != X509_set_issuer_name(cert, name)) {
        return error_code::tls_cert_set_failure;
    }

    X509V3_CTX ctx;
    ctx.db = nullptr; /* no DB is used */
    X509V3_set_ctx(&ctx, cert, cert, nullptr, nullptr, 0);

    if (!add_extension(ctx, cert, NID_basic_constraints, "critical, CA:false")) {
        return error_code::tls_cert_ext_failure;
    }

    if (!add_extension(ctx, cert, NID_key_usage, "critical, Digital Signature, Key Encipherment")) {
        return error_code::tls_cert_ext_failure;
    }

    if (!add_extension(ctx, cert, NID_ext_key_usage, "TLS Web Server Authentication, TLS Web Client Authentication")) {
        return error_code::tls_cert_ext_failure;
    }

    auto bytes = X509_sign(cert, pkey, EVP_sha256());
    if (!bytes) {
        return error_code::tls_cert_sign_failure;
    }

    auto cert_container = as_der(cert);
    if (!cert_container) {
        return cert_container.error();
    }

    auto key_container = as_der(pkey);
    if (!key_container) {
        return key_container.error();
    }

    return key_pair_t{std::move(cert_guard), std::move(pkey_quard), std::move(cert_container.value()),
                      std::move(key_container.value())};
}

outcome::result<void> key_pair_t::save(const char *cert_path, const char *priv_key_path) const noexcept {
    do {
        auto cert_file = fopen(cert_path, "w");
        if (!cert_file) {
            return sys::error_code{errno, sys::generic_category()};
        }
        auto cert_file_guard = make_guard(cert_file, [](auto *ptr) { fclose(ptr); });

        if (1 != PEM_write_X509(cert_file, cert.get())) {
            return error_code::tls_cert_save_failure;
        }
    } while (0);

    do {
        auto pk_file = fopen(priv_key_path, "w");
        if (!pk_file) {
            return sys::error_code{errno, sys::generic_category()};
        }
        auto pk_file_guard = make_guard(pk_file, [](auto *ptr) { fclose(ptr); });
        if (1 != PEM_write_PrivateKey(pk_file, private_key.get(), nullptr, nullptr, 0, nullptr, nullptr)) {
            return error_code::tls_key_save_failure;
        }
    } while (0);

    return outcome::success();
}

outcome::result<key_pair_t> load_pair(const char *cert_path, const char *priv_key_path) noexcept {
    /* read certificate in memory, then load it va openssl */
    auto cert_file = fopen(cert_path, "r");
    if (!cert_file) {
        return sys::error_code{errno, sys::generic_category()};
    }
    auto cert_file_guard = make_guard(cert_file, [](auto *ptr) { fclose(ptr); });

    if (0 != fseek(cert_file, 0L, SEEK_END)) {
        return sys::error_code{errno, sys::generic_category()};
    }
    auto cert_sz = ftell(cert_file);
    if (cert_sz < 0) {
        return sys::error_code{errno, sys::generic_category()};
    }
    rewind(cert_file);

    X509 *cert = X509_new();
    auto cert_guard = make_guard(cert, [](auto *ptr) { X509_free(ptr); });
    if (!PEM_read_X509(cert_file, &cert, nullptr, nullptr)) {
        return error_code::tls_cert_load_failure;
    }

    /* read private key */
    auto pk_file = fopen(priv_key_path, "r");
    if (!pk_file) {
        return sys::error_code{errno, sys::generic_category()};
    }
    auto pk_file_guard = make_guard(pk_file, [](auto *ptr) { fclose(ptr); });
    EVP_PKEY *pkey = EVP_PKEY_new();
    auto pkey_quard = make_guard(pkey, [](auto *ptr) { EVP_PKEY_free(ptr); });
    if (!PEM_read_PrivateKey(pk_file, &pkey, nullptr, nullptr)) {
        return error_code::tls_cert_load_failure;
    }

    auto cert_container = as_der(cert);
    if (!cert_container) {
        return cert_container.error();
    }

    auto key_container = as_der(pkey);
    if (!key_container) {
        return key_container.error();
    }

    return key_pair_t{std::move(cert_guard), std::move(pkey_quard), std::move(cert_container.value()),
                      std::move(key_container.value())};
}

outcome::result<std::string> sha256_digest(const std::string &data) noexcept {
    unsigned char buff[SHA256_DIGEST_LENGTH];
    SHA256_CTX sha256;
    if (1 != SHA256_Init(&sha256)) {
        return error_code::tls_sha256_init_failure;
    }

    if (1 != SHA256_Update(&sha256, data.c_str(), data.size())) {
        return error_code::tls_sha256_failure;
    }

    SHA256_Final(buff, &sha256);
    return std::string(buff, buff + SHA256_DIGEST_LENGTH);
}

} // namespace syncspirit::utils
