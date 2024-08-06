// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include "generic_diff.hpp"
#include "syncspirit-export.h"

namespace syncspirit::model::diff {

struct cluster_diff_t;
using cluster_diff_ptr_t = boost::intrusive_ptr<cluster_diff_t>;

struct SYNCSPIRIT_API cluster_diff_t : generic_diff_t<tag::cluster, cluster_diff_t> {
    using parent_t = generic_diff_t<tag::cluster, cluster_diff_t>;
    using visitor_t = parent_t::visitor_t;
    using parent_t::parent_t;
};

using cluster_visitor_t = cluster_diff_t::visitor_t;

} // namespace syncspirit::model::diff
