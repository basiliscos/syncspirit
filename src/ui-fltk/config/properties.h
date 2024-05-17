#pragma once

#include "property.h"

namespace syncspirit::fltk::config {

namespace impl {
struct positive_integer_t : property_t {
    positive_integer_t(std::string label, std::string explanation, std::string value, std::string default_value);

    error_ptr_t validate_value() noexcept override;

    std::uint32_t native_value;
};
} // namespace impl

namespace db {

struct uncommited_threshold_t final : impl::positive_integer_t {
    using parent_t = impl::positive_integer_t;

    static const char *explanation_;

    uncommited_threshold_t(std::uint32_t value, std::uint32_t default_value);
    void reflect_to(syncspirit::config::main_t &main) override;
};

struct upper_limit_t final : impl::positive_integer_t {
    using parent_t = impl::positive_integer_t;

    static const char *explanation_;

    upper_limit_t(std::uint32_t value, std::uint32_t default_value);
    void reflect_to(syncspirit::config::main_t &main) override;
};

} // namespace db
} // namespace syncspirit::fltk::config
