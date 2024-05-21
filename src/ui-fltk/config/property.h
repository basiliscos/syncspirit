#pragma once

#include "config/main.h"
#include <string>
#include <memory>
#include <boost/intrusive_ptr.hpp>
#include <boost/smart_ptr/intrusive_ref_counter.hpp>

namespace syncspirit::fltk::config {

using main_cfg_t = syncspirit::config::main_t;
using error_ptr_t = std::unique_ptr<std::string>;

enum class property_kind_t {
    text,
    file,
    directory,
    positive_integer,
    boolean,
};

struct property_t : boost::intrusive_ref_counter<property_t, boost::thread_unsafe_counter> {
    using parent_t = boost::intrusive_ref_counter<property_t, boost::thread_unsafe_counter>;
    std::string_view get_label() const noexcept;
    std::string_view get_explanation() const noexcept;
    std::string_view get_value() const noexcept;
    property_kind_t get_kind() const noexcept;
    void set_value(std::string_view) noexcept;
    void reset() noexcept;
    void undo() noexcept;
    const error_ptr_t &validate() noexcept;
    bool same_as_initial() const noexcept;
    bool same_as_default() const noexcept;
    virtual void reflect_to(syncspirit::config::main_t &main) = 0;

  protected:
    property_t(std::string label, std::string explanation, std::string value, std::string default_value,
               property_kind_t kind);
    virtual error_ptr_t validate_value() noexcept;

    std::string label;
    std::string explanation;
    std::string value;
    std::string initial_value;
    std::string default_value;
    property_kind_t kind;
    error_ptr_t error;
};

using property_ptr_t = boost::intrusive_ptr<property_t>;

} // namespace syncspirit::fltk::config
