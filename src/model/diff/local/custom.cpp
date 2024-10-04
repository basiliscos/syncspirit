// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#include "custom.h"
#include "model/diff/cluster_visitor.h"

using namespace syncspirit::model::diff::local;

auto custom_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting custom_t");
    return visitor(*this, custom);
}
