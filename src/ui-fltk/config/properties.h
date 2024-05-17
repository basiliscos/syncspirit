#pragma once

#include "property.h"

namespace syncspirit::fltk::config {

namespace db {
struct uncommited_threshold_t final : property_t {

    static const char *explanation_;

    uncommited_threshold_t(std::uint32_t value, std::uint32_t default_value);
    void reflect_to(syncspirit::config::main_t &main) override;

    error_ptr_t validate_value() noexcept override;

    std::uint32_t native_value;
};

} // namespace db
} // namespace syncspirit::fltk::config
