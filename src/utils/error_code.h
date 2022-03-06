// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#pragma once
#include <string>
#include <system_error>
#include <boost/system/error_code.hpp>

namespace syncspirit::utils {

enum class error_code_t {
    success = 0,
    incomplete_discovery_reply,
    no_location,
    no_st,
    no_usn,
    igd_mismatch,
    xml_parse_error,
    wan_notfound,
    timed_out,
    service_not_available,
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
    tls_cn_missing,
    base32_decoding_failure,
    unexpected_response_code,
    negative_reannounce_interval,
    malformed_json,
    incorrect_json,
    malformed_url,
    malformed_date,
    transport_not_available,
    wrong_magic,
    cannot_connect_to_peer,
    announce_failed,
    discovery_failed,
    endpoint_failed,
    portmapping_failed,
    connection_impossible,
    missing_device_id,
    missing_cn,
    rx_limit_reached,
    non_authorized,
    igd_description_failed,
    unparseable_control_url,
    external_ip_failed,
    rx_timeout,
    fs_error,
    scan_aborted,
    already_shared,
    unknown_sink,
    misconfigured_default_logger,
    already_connected,
};

enum class bep_error_code_t {
    success = 0,
    protobuf_err,
    lz4_decoding,
    unexpected_message,
    unexpected_response,
    response_mismatch,
    response_missize,
};

enum class protocol_error_code_t {
    success = 0,
    unknown_folder,
    digest_mismatch,
};

enum class request_error_code_t {
    success = 0,
    generic = 1,
    no_such_file = 2,
    invalid_file = 3,
};

namespace detail {

class error_code_category : public boost::system::error_category {
    virtual const char *name() const noexcept override;
    virtual std::string message(int c) const override;
};

class bep_error_code_category : public boost::system::error_category {
    virtual const char *name() const noexcept override;
    virtual std::string message(int c) const override;
};

class protocol_error_code_category : public boost::system::error_category {
    virtual const char *name() const noexcept override;
    virtual std::string message(int c) const override;
};

class request_error_code_category : public boost::system::error_category {
    virtual const char *name() const noexcept override;
    virtual std::string message(int c) const override;
};

} // namespace detail

const detail::error_code_category &error_code_category();

const detail::bep_error_code_category &bep_error_code_category();

const detail::protocol_error_code_category &protocol_error_code_category();

const detail::request_error_code_category &request_error_code_category();

inline boost::system::error_code make_error_code(error_code_t e) {
    return {static_cast<int>(e), error_code_category()};
}
inline boost::system::error_code make_error_code(bep_error_code_t e) {
    return {static_cast<int>(e), bep_error_code_category()};
}

inline boost::system::error_code make_error_code(protocol_error_code_t e) {
    return {static_cast<int>(e), protocol_error_code_category()};
}

inline boost::system::error_code make_error_code(request_error_code_t e) {
    return {static_cast<int>(e), request_error_code_category()};
}

boost::system::error_code adapt(const std::error_code &ec) noexcept;

} // namespace syncspirit::utils

namespace std {
template <> struct is_error_code_enum<syncspirit::utils::error_code_t> : std::true_type {};
} // namespace std

namespace boost {
namespace system {

template <> struct is_error_code_enum<syncspirit::utils::error_code_t> : std::true_type {
    static const bool value = true;
};

template <> struct is_error_code_enum<syncspirit::utils::bep_error_code_t> : std::true_type {
    static const bool value = true;
};

template <> struct is_error_code_enum<syncspirit::utils::protocol_error_code_t> : std::true_type {
    static const bool value = true;
};

template <> struct is_error_code_enum<syncspirit::utils::request_error_code_t> : std::true_type {
    static const bool value = true;
};

} // namespace system
} // namespace boost
