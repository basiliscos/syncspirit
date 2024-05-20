#include "property.h"

using namespace syncspirit::fltk::config;

property_t::property_t(std::string label_, std::string explanation_, std::string value_, std::string default_value_,
                       property_kind_t kind_)
    : label{label_}, explanation{explanation_}, value{value_}, initial_value{value_}, default_value{default_value_},
      kind{kind_} {}

std::string_view property_t::get_label() const noexcept { return label; }

std::string_view property_t::get_explanation() const noexcept { return explanation; }

std::string_view property_t::get_value() const noexcept { return value; }

property_kind_t property_t::get_kind() const noexcept { return kind; }

void property_t::set_value(std::string_view value_) noexcept {
    value = value_;
    error = validate_value();
}

void property_t::reset() noexcept { value = default_value; }

void property_t::undo() noexcept { value = initial_value; }

const error_ptr_t &property_t::validate() noexcept { return error; }

error_ptr_t property_t::validate_value() noexcept { return {}; }

bool property_t::same_as_initial() const noexcept { return initial_value == value; }

bool property_t::same_as_default() const noexcept { return default_value == value; }
