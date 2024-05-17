#include "properties.h"

namespace syncspirit::fltk::config {

namespace db {

uncommited_threshold_t::uncommited_threshold_t(std::uint32_t value, std::uint32_t default_value)
    : property_t("uncommited_threshold", explanation_, std::to_string(value), std::to_string(default_value),
                 property_kind_t::positive_integer) {}

void uncommited_threshold_t::reflect_to(syncspirit::config::main_t &main) {
    main.db_config.uncommitted_threshold = native_value;
}

error_ptr_t uncommited_threshold_t::validate_value() noexcept {
    int r;
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
    native_value = static_cast<std::uint32_t>(r);
    return {};
}

const char *uncommited_threshold_t::explanation_ = "how many transactions keep in memory before flushing to storage";

} // namespace db

} // namespace syncspirit::fltk::config
