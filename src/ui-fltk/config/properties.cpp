#include "properties.h"

namespace syncspirit::fltk::config {

namespace impl {

positive_integer_t::positive_integer_t(std::string label, std::string explanation, std::string value,
                                       std::string default_value)
    : property_t(std::move(label), std::move(explanation), std::move(value), std::move(default_value),
                 property_kind_t::positive_integer) {}

error_ptr_t positive_integer_t::validate_value() noexcept {
    std::uint64_t r;
    auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), r);

    if (ec == std::errc::invalid_argument) {
        return error_ptr_t(new std::string("not a number"));
    } else if (ec == std::errc::result_out_of_range) {
        return error_ptr_t(new std::string("too large number"));
    }
    assert(ec == std::errc());

    if (r <= 0) {
        return error_ptr_t(new std::string("not a positive number"));
    }

    // all ok
    native_value = static_cast<std::uint64_t>(r);
    return {};
}
} // namespace impl

namespace db {

uncommited_threshold_t::uncommited_threshold_t(std::uint64_t value, std::uint64_t default_value)
    : parent_t("uncommited_threshold", explanation_, std::to_string(value), std::to_string(default_value)) {}

void uncommited_threshold_t::reflect_to(syncspirit::config::main_t &main) {
    main.db_config.uncommitted_threshold = native_value;
}

const char *uncommited_threshold_t::explanation_ = "how many transactions keep in memory before flushing to storage";

upper_limit_t::upper_limit_t(std::uint64_t value, std::uint64_t default_value)
    : parent_t("upper_limit", explanation_, std::to_string(value), std::to_string(default_value)) {}

void upper_limit_t::reflect_to(syncspirit::config::main_t &main) {
    main.db_config.uncommitted_threshold = native_value;
}

const char *upper_limit_t::explanation_ = "maximum database size, in bytes";

} // namespace db

} // namespace syncspirit::fltk::config
