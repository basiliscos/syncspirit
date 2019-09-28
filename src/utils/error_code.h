#pragma once
#include <string>
#include <system_error>
#include <boost/system/error_code.hpp>

namespace syncspirit::utils {

enum class error_code {
    success = 0,
    incomplete_discovery_reply,
    no_location,
    no_st,
    no_usn,
    igd_mismatch,
    xml_parse_error,
    wan_notfound,
    timed_out,
    cant_determine_config_dir,
    tls_context_init_failure,
    tls_param_init_failure,
    tls_param_gen_failure,
    tls_key_gen_init_failure,
    tls_key_gen_failure,
    tls_ec_curve_failure,
    tls_ec_group_failure,
    tls_cert_set_failure,
    tls_cert_ext_failure,
    tls_cert_sign_failure,
    tls_cert_save_failure,
    tls_cert_load_failure,
    tls_key_save_failure,
    tls_key_load_failure,
    tls_sha256_init_failure,
    tls_sha256_failure,
};

namespace detail {

// class error_code_category : public std::error_category {
class error_code_category : public boost::system::error_category {
    virtual const char *name() const noexcept override;
    virtual std::string message(int c) const override;
};

} // namespace detail

const detail::error_code_category &error_code_category();

// inline std::error_code make_error_code(error_code e) { return {static_cast<int>(e), error_code_category()}; }
inline boost::system::error_code make_error_code(error_code e) { return {static_cast<int>(e), error_code_category()}; }

} // namespace syncspirit::utils

/*
namespace std {
template <> struct is_error_code_enum<syncspirit::utils::error_code> : std::true_type {};
} // namespace std

*/

namespace boost {
namespace system {

template <> struct is_error_code_enum<syncspirit::utils::error_code> : std::true_type {
    static const bool value = true;
};

} // namespace system
} // namespace boost
