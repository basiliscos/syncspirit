// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include "generic_diff.hpp"
#include "cluster_visitor.h"
#include "syncspirit-export.h"

namespace syncspirit::model::diff {

struct cluster_diff_t;
using cluster_diff_ptr_t = boost::intrusive_ptr<cluster_diff_t>;

struct SYNCSPIRIT_API cluster_diff_t : generic_diff_t<tag::cluster> {
    virtual outcome::result<void> visit(cluster_visitor_t &, void *custom) const noexcept override;

    cluster_diff_ptr_t inner;
};

} // namespace syncspirit::model::diff
