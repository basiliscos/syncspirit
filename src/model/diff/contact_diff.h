// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include "generic_diff.hpp"

namespace syncspirit::model::diff {

struct contact_diff_t;
using contact_diff_ptr_t = boost::intrusive_ptr<contact_diff_t>;

struct SYNCSPIRIT_API contact_diff_t : generic_diff_t<tag::contact, contact_diff_t> {
#if 0
    virtual outcome::result<void> visit(visitor_t &, void *custom) const noexcept override;

    contact_diff_t *assign_sibling(contact_diff_t *sibling) noexcept;
    void assign_child(contact_diff_ptr_t child) noexcept;

    contact_diff_ptr_t child;
    contact_diff_ptr_t sibling;
#endif
};

using contact_visitor_t = contact_diff_t::visitor_t;

} // namespace syncspirit::model::diff
