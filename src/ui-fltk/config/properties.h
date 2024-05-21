#pragma once

#include "property.h"

namespace syncspirit::fltk::config {

namespace impl {
struct positive_integer_t : property_t {
    positive_integer_t(std::string label, std::string explanation, std::string value, std::string default_value);

    error_ptr_t validate_value() noexcept override;

    std::uint64_t native_value;
};

struct string_t : property_t {
    string_t(std::string label, std::string explanation, std::string value, std::string default_value);
};

struct url_t : string_t {
    using parent_t = string_t;
    using parent_t::parent_t;
    error_ptr_t validate_value() noexcept override;
};

struct path_t : property_t {
    path_t(std::string label, std::string explanation, std::string value, std::string default_value,
           property_kind_t kind = property_kind_t::file);
};

struct bool_t : property_t {
    bool_t(bool value, bool default_value, std::string label = "enabled");
    error_ptr_t validate_value() noexcept override;

    std::uint64_t native_value;
};

} // namespace impl

namespace bep {

struct blocks_max_requested_t final : impl::positive_integer_t {
    using parent_t = impl::positive_integer_t;

    static const char *explanation_;

    blocks_max_requested_t(std::uint64_t value, std::uint64_t default_value);
    void reflect_to(syncspirit::config::main_t &main) override;
};

struct blocks_simultaneous_write_t final : impl::positive_integer_t {
    using parent_t = impl::positive_integer_t;

    static const char *explanation_;

    blocks_simultaneous_write_t(std::uint64_t value, std::uint64_t default_value);
    void reflect_to(syncspirit::config::main_t &main) override;
};

struct connect_timeout_t final : impl::positive_integer_t {
    using parent_t = impl::positive_integer_t;

    static const char *explanation_;

    connect_timeout_t(std::uint64_t value, std::uint64_t default_value);
    void reflect_to(syncspirit::config::main_t &main) override;
};

struct request_timeout_t final : impl::positive_integer_t {
    using parent_t = impl::positive_integer_t;

    static const char *explanation_;

    request_timeout_t(std::uint64_t value, std::uint64_t default_value);
    void reflect_to(syncspirit::config::main_t &main) override;
};

struct rx_buff_size_t final : impl::positive_integer_t {
    using parent_t = impl::positive_integer_t;

    static const char *explanation_;

    rx_buff_size_t(std::uint64_t value, std::uint64_t default_value);
    void reflect_to(syncspirit::config::main_t &main) override;
};

struct rx_timeout_t final : impl::positive_integer_t {
    using parent_t = impl::positive_integer_t;

    static const char *explanation_;

    rx_timeout_t(std::uint64_t value, std::uint64_t default_value);
    void reflect_to(syncspirit::config::main_t &main) override;
};

struct tx_buff_limit_t final : impl::positive_integer_t {
    using parent_t = impl::positive_integer_t;

    static const char *explanation_;

    tx_buff_limit_t(std::uint64_t value, std::uint64_t default_value);
    void reflect_to(syncspirit::config::main_t &main) override;
};

struct tx_timeout_t final : impl::positive_integer_t {
    using parent_t = impl::positive_integer_t;

    static const char *explanation_;

    tx_timeout_t(std::uint64_t value, std::uint64_t default_value);
    void reflect_to(syncspirit::config::main_t &main) override;
};

} // namespace bep

namespace db {

struct uncommited_threshold_t final : impl::positive_integer_t {
    using parent_t = impl::positive_integer_t;

    static const char *explanation_;

    uncommited_threshold_t(std::uint64_t value, std::uint64_t default_value);
    void reflect_to(syncspirit::config::main_t &main) override;
};

struct upper_limit_t final : impl::positive_integer_t {
    using parent_t = impl::positive_integer_t;

    static const char *explanation_;

    upper_limit_t(std::uint64_t value, std::uint64_t default_value);
    void reflect_to(syncspirit::config::main_t &main) override;
};

} // namespace db

namespace dialer {

struct enabled_t final : impl::bool_t {
    using parent_t = impl::bool_t;
    using parent_t::parent_t;

    void reflect_to(syncspirit::config::main_t &main) override;
};

struct redial_timeout_t final : impl::positive_integer_t {
    using parent_t = impl::positive_integer_t;

    static const char *explanation_;

    redial_timeout_t(std::uint64_t value, std::uint64_t default_value);
    void reflect_to(syncspirit::config::main_t &main) override;
};

} // namespace dialer

namespace fs {

struct mru_size_t final : impl::positive_integer_t {
    using parent_t = impl::positive_integer_t;

    static const char *explanation_;

    mru_size_t(std::uint64_t value, std::uint64_t default_value);
    void reflect_to(syncspirit::config::main_t &main) override;
};

struct temporally_timeout_t final : impl::positive_integer_t {
    using parent_t = impl::positive_integer_t;

    static const char *explanation_;

    temporally_timeout_t(std::uint64_t value, std::uint64_t default_value);
    void reflect_to(syncspirit::config::main_t &main) override;
};

} // namespace fs

namespace global_discovery {

struct enabled_t final : impl::bool_t {
    using parent_t = impl::bool_t;
    using parent_t::parent_t;

    void reflect_to(syncspirit::config::main_t &main) override;
};

struct announce_url_t final : impl::url_t {
    using parent_t = impl::url_t;

    static const char *explanation_;

    announce_url_t(std::string value, std::string default_value);

    void reflect_to(syncspirit::config::main_t &main) override;
};

struct cert_file_t final : impl::path_t {
    using parent_t = impl::path_t;

    static const char *explanation_;

    cert_file_t(std::string value, std::string default_value);

    void reflect_to(syncspirit::config::main_t &main) override;
};

struct device_id_t final : impl::string_t {
    using parent_t = impl::string_t;

    static const char *explanation_;

    device_id_t(std::string value, std::string default_value);

    void reflect_to(syncspirit::config::main_t &main) override;
};

struct key_file_t final : impl::path_t {
    using parent_t = impl::path_t;

    static const char *explanation_;

    key_file_t(std::string value, std::string default_value);

    void reflect_to(syncspirit::config::main_t &main) override;
};

struct rx_buff_size_t final : impl::positive_integer_t {
    using parent_t = impl::positive_integer_t;

    static const char *explanation_;

    rx_buff_size_t(std::uint64_t value, std::uint64_t default_value);
    void reflect_to(syncspirit::config::main_t &main) override;
};

struct timeout_t final : impl::positive_integer_t {
    using parent_t = impl::positive_integer_t;

    static const char *explanation_;

    timeout_t(std::uint64_t value, std::uint64_t default_value);
    void reflect_to(syncspirit::config::main_t &main) override;
};

} // namespace global_discovery

namespace local_discovery {

struct enabled_t final : impl::bool_t {
    using parent_t = impl::bool_t;
    using parent_t::parent_t;

    void reflect_to(syncspirit::config::main_t &main) override;
};

struct frequency_t final : impl::positive_integer_t {
    using parent_t = impl::positive_integer_t;

    static const char *explanation_;

    frequency_t(std::uint64_t value, std::uint64_t default_value);
    void reflect_to(syncspirit::config::main_t &main) override;
};

struct port_t final : impl::positive_integer_t {
    using parent_t = impl::positive_integer_t;

    static const char *explanation_;

    port_t(std::uint64_t value, std::uint64_t default_value);
    void reflect_to(syncspirit::config::main_t &main) override;
};

} // namespace local_discovery

namespace main {

struct default_location_t final : impl::path_t {
    using parent_t = impl::path_t;

    static const char *explanation_;

    default_location_t(std::string value, std::string default_value);

    void reflect_to(syncspirit::config::main_t &main) override;
};

struct device_name_t final : impl::string_t {
    using parent_t = impl::string_t;

    static const char *explanation_;

    device_name_t(std::string value, std::string default_value);

    void reflect_to(syncspirit::config::main_t &main) override;
};

struct hasher_threads_t final : impl::positive_integer_t {
    using parent_t = impl::positive_integer_t;

    static const char *explanation_;

    hasher_threads_t(std::uint64_t value, std::uint64_t default_value);
    void reflect_to(syncspirit::config::main_t &main) override;
};

struct timeout_t final : impl::positive_integer_t {
    using parent_t = impl::positive_integer_t;

    static const char *explanation_;

    timeout_t(std::uint64_t value, std::uint64_t default_value);
    void reflect_to(syncspirit::config::main_t &main) override;
};

} // namespace main

namespace relay {

struct enabled_t final : impl::bool_t {
    using parent_t = impl::bool_t;
    using parent_t::parent_t;

    void reflect_to(syncspirit::config::main_t &main) override;
};

struct discovery_url_t final : impl::url_t {
    using parent_t = impl::url_t;

    static const char *explanation_;

    discovery_url_t(std::string value, std::string default_value);

    void reflect_to(syncspirit::config::main_t &main) override;
};

struct rx_buff_size_t final : impl::positive_integer_t {
    using parent_t = impl::positive_integer_t;

    static const char *explanation_;

    rx_buff_size_t(std::uint64_t value, std::uint64_t default_value);
    void reflect_to(syncspirit::config::main_t &main) override;
};

} // namespace relay

namespace upnp {

struct enabled_t final : impl::bool_t {
    using parent_t = impl::bool_t;
    using parent_t::parent_t;

    void reflect_to(syncspirit::config::main_t &main) override;
};

struct debug_t final : impl::bool_t {
    using parent_t = impl::bool_t;

    debug_t(bool value, bool default_value);
    void reflect_to(syncspirit::config::main_t &main) override;
};

struct external_port_t final : impl::positive_integer_t {
    using parent_t = impl::positive_integer_t;

    static const char *explanation_;

    external_port_t(std::uint64_t value, std::uint64_t default_value);
    void reflect_to(syncspirit::config::main_t &main) override;
};

struct max_wait_t final : impl::positive_integer_t {
    using parent_t = impl::positive_integer_t;

    static const char *explanation_;

    max_wait_t(std::uint64_t value, std::uint64_t default_value);
    void reflect_to(syncspirit::config::main_t &main) override;
};

struct rx_buff_size_t final : impl::positive_integer_t {
    using parent_t = impl::positive_integer_t;

    static const char *explanation_;

    rx_buff_size_t(std::uint64_t value, std::uint64_t default_value);
    void reflect_to(syncspirit::config::main_t &main) override;
};

} // namespace upnp

} // namespace syncspirit::fltk::config
