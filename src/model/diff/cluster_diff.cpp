// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2023 Ivan Baidakou

#include "cluster_diff.h"
#include "../cluster.h"

using namespace syncspirit::model::diff;

auto cluster_diff_t::visit(cluster_visitor_t &, void *) const noexcept -> outcome::result<void> {
    return outcome::success();
}
