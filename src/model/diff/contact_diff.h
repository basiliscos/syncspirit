// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#pragma once

#include "generic_diff.hpp"
#include "aggregate_diff.hpp"
#include "contact_visitor.h"

namespace syncspirit::model::diff {

struct SYNCSPIRIT_API contact_diff_t : generic_diff_t<tag::contact> {
    virtual outcome::result<void> visit(contact_visitor_t &, void *custom) const noexcept override;
};

using contact_diff_ptr_t = boost::intrusive_ptr<contact_diff_t>;

using contact_aggregate_diff_t = aggregate_diff_t<contact_diff_t>;

} // namespace syncspirit::model::diff
