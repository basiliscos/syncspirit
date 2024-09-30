// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#pragma once

#include "../cluster_diff.h"

namespace syncspirit::model::diff::local {

struct SYNCSPIRIT_API custom_t : cluster_diff_t {
    custom_t() = default;
    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;
};

} // namespace syncspirit::model::diff::local
