// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#pragma once

#include "generic_remove.h"

namespace syncspirit::model::diff::modify {

struct SYNCSPIRIT_API remove_pending_folders_t final : generic_remove_t {
    using generic_remove_t::generic_remove_t;
    outcome::result<void> apply_impl(apply_controller_t &, void *) const noexcept override;
    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;
};

} // namespace syncspirit::model::diff::modify
