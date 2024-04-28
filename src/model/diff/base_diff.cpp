// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#include "base_diff.h"
#include "../cluster.h"

using namespace syncspirit::model::diff;

base_diff_t::base_diff_t() noexcept { log = get_log(); }

auto base_diff_t::get_log() noexcept -> utils::logger_t { return utils::get_logger("model.diff"); }

auto base_diff_t::apply(cluster_t &cluster) const noexcept -> outcome::result<void> {
    auto r = apply_impl(cluster);
    if (!r) {
        cluster.mark_tainted();
    }
    return r;
}
