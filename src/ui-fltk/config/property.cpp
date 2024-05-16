#include "property.h"

using namespace syncspirit::fltk::config;

property_t::property_t(std::string label_, std::string explanation_, std::string value_, std::string default_value_)
    : label{label_}, explanation{explanation_}, value{value_}, default_value{default_value_} {}

std::string_view property_t::get_label() const noexcept { return label; }

std::string_view property_t::get_explanation() const noexcept { return explanation; }

std::string_view property_t::get_value() const noexcept { return explanation; }

void property_t::set_value(std::string_view value_) noexcept {
    value = value_;
    error = validate_value();
}

void property_t::reset() noexcept { value = default_value; }

void property_t::undo() noexcept { value = initial_value; }

const error_ptr_t &property_t::validate() noexcept { return error; }
