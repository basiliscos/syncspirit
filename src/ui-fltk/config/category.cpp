#include "category.h"

#include "properties.h"

#include <charconv>
#include <cassert>

namespace syncspirit::fltk::config {

category_t::category_t(std::string label_, std::string explanation_, properties_t properties_)
    : label{std::move(label_)}, explanation{std::move(explanation_)}, properties{std::move(properties_)} {}

const std::string &category_t::get_label() const { return label; }

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
