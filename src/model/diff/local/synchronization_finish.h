// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#pragma once

#include <string>
#include "../cluster_diff.h"

namespace syncspirit::model::diff::local {

struct SYNCSPIRIT_API synchronization_finish_t final : cluster_diff_t {
    synchronization_finish_t(std::string_view folder_id);
    outcome::result<void> apply_impl(apply_controller_t &, void *) const noexcept override;
    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;

    std::string folder_id;
};

} // namespace syncspirit::model::diff::local
