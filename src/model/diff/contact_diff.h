// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include "generic_diff.hpp"
#include "contact_visitor.h"

namespace syncspirit::model::diff {

struct contact_diff_t;
using contact_diff_ptr_t = boost::intrusive_ptr<contact_diff_t>;

struct SYNCSPIRIT_API contact_diff_t : generic_diff_t<tag::contact> {
    virtual outcome::result<void> visit(contact_visitor_t &, void *custom) const noexcept override;
    contact_diff_t *assign(contact_diff_t *next) noexcept;

    contact_diff_ptr_t next;
};

} // namespace syncspirit::model::diff
