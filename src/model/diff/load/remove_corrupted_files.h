// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include "../modify/generic_remove.h"

namespace syncspirit::model::diff::load {

struct SYNCSPIRIT_API remove_corrupted_files_t final : modify::generic_remove_t {
    using parent_t = modify::generic_remove_t;
    using parent_t::parent_t;

    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;
};

} // namespace syncspirit::model::diff::load
