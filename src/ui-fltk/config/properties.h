#pragma once

#include "property.h"

namespace syncspirit::fltk::config {

namespace impl {
struct positive_integer_t : property_t {
    positive_integer_t(std::string label, std::string explanation, std::string value, std::string default_value);

    error_ptr_t validate_value() noexcept override;

    std::uint64_t native_value;
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

} // namespace syncspirit::fltk::config
