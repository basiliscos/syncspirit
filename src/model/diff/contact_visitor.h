#pragma once
#include "generic_diff.hpp"

namespace syncspirit::model::diff {

namespace modify {
    struct update_contact_t;
    struct connect_request_t;
}


template<>
struct generic_visitor_t<tag::contact> {
    virtual ~generic_visitor_t() = default;

    virtual outcome::result<void> operator()(const modify::update_contact_t &) noexcept;
    virtual outcome::result<void> operator()(const modify::connect_request_t &) noexcept;
};

using contact_visitor_t = generic_visitor_t<tag::contact>;

}
