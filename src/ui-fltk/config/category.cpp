#include "category.h"

#include <charconv>
#include <cassert>

namespace syncspirit::fltk::config {

category_t::category_t(std::string label_, std::string explanation_, properties_t properties_)
    : label{std::move(label_)}, explanation{std::move(explanation_)}, properties{std::move(properties_)} {}

namespace {

namespace db {
struct uncommited_threshold_t final : property_t {

    static const char *explanation_;

    uncommited_threshold_t(std::uint32_t value, std::uint32_t default_value)
        : property_t("uncommited_threshold", explanation_, std::to_string(value), std::to_string(default_value)) {}

    void reflect_to(syncspirit::config::main_t &main) override { main.db_config.uncommitted_threshold = native_value; }

    error_ptr_t validate_value() noexcept override {
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

    std::uint32_t native_value;
};

const char *uncommited_threshold_t::explanation_ = "how many transactions keep in memory before flushing to storage";

} // namespace db

} // namespace

auto reflect(const main_cfg_t &config, const main_cfg_t &default_config) -> categories_t {
    auto r = categories_t{};

    auto c_db = [&]() -> category_ptr_t {
        auto props = properties_t{property_ptr_t(new db::uncommited_threshold_t(
            config.db_config.uncommitted_threshold, default_config.db_config.uncommitted_threshold))};

        return new category_t("db", "database related settings", std::move(props));
    }();
    r.push_back(std::move(c_db));

    return r;
}

} // namespace syncspirit::fltk::config
