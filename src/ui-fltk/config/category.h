#pragma once

#include "property.h"
#include <vector>

namespace syncspirit::fltk::config {

using properties_t = std::vector<property_ptr_t>;

struct category_t : boost::intrusive_ref_counter<category_t, boost::thread_unsafe_counter> {
    using parent_t = boost::intrusive_ref_counter<category_t, boost::thread_unsafe_counter>;

    category_t(std::string label, std::string explanation, properties_t properties);
    const std::string &get_label();
    const std::string &get_explanation();
    const properties_t &get_properties();

  private:
    std::string label;
    std::string explanation;
    properties_t properties;
};

using category_ptr_t = boost::intrusive_ptr<category_t>;
using categories_t = std::vector<category_ptr_t>;

auto reflect(const main_cfg_t &config, const main_cfg_t &default_config) -> categories_t;
auto reflect(const categories_t &) -> main_cfg_t;

} // namespace syncspirit::fltk::config
