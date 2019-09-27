#include "tls.h"
#include "error_code.h"
#include <random>

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

outcome::result<key_pair_t> generate_pair(const char *issuer_name) noexcept {
    auto ev_ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr);
    if (ev_ctx == nullptr) {
        return error_code::tls_context_init_failure;
    }
    auto ev_ctx_quard = make_guard(ev_ctx, [](auto *ptr) { EVP_PKEY_CTX_free(ptr); });

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

    return key_pair_t{std::move(cert_guard), std::move(pkey_quard)};
}

} // namespace syncspirit::utils
