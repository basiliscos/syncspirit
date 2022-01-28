#pragma once

#include "generic_diff.hpp"
#include "contact_visitor.h"

namespace syncspirit::model::diff {

struct contact_diff_t: generic_diff_t<tag::contact> {
    virtual outcome::result<void> visit(contact_visitor_t &) const noexcept override;
};

using contact_diff_ptr_t = boost::intrusive_ptr<contact_diff_t>;

}
