#pragma once

#include "config/main.h"
#include <string>
#include <memory>
#include <boost/intrusive_ptr.hpp>
#include <boost/smart_ptr/intrusive_ref_counter.hpp>

namespace syncspirit::fltk::config {

using main_cfg_t = syncspirit::config::main_t;
using error_ptr_t = std::unique_ptr<std::string>;

struct property_t : boost::intrusive_ref_counter<property_t, boost::thread_unsafe_counter> {
    using parent_t = boost::intrusive_ref_counter<property_t, boost::thread_unsafe_counter>;
    std::string_view get_label() const noexcept;
    std::string_view get_explanation() const noexcept;
    std::string_view get_value() const noexcept;
    void set_value(std::string_view) noexcept;
    void reset() noexcept;
    void undo() noexcept;
    const error_ptr_t &validate() noexcept;
    virtual void reflect_to(syncspirit::config::main_t &main) = 0;

  protected:
    property_t(std::string label, std::string explanation, std::string value, std::string default_value);
    virtual error_ptr_t validate_value() noexcept = 0;

    std::string label;
    std::string explanation;
    std::string value;
    std::string initial_value;
    std::string default_value;
    error_ptr_t error;
};

using property_ptr_t = boost::intrusive_ptr<property_t>;

} // namespace syncspirit::fltk::config
