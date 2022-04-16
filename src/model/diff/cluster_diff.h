// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#pragma once

#include "generic_diff.hpp"
#include "cluster_visitor.h"
#include "syncspirit-export.h"

namespace syncspirit::model::diff {

struct SYNCSPIRIT_API cluster_diff_t : generic_diff_t<tag::cluster> {
    virtual outcome::result<void> visit(cluster_visitor_t &) const noexcept override;
};

using cluster_diff_ptr_t = boost::intrusive_ptr<cluster_diff_t>;

} // namespace syncspirit::model::diff
