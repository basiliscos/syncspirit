// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include "../cluster_diff.h"
#include "model/folder.h"

namespace syncspirit::model::diff::modify {

struct SYNCSPIRIT_API suspend_folder_t final : cluster_diff_t {

    suspend_folder_t(const model::folder_t &folder) noexcept;

    outcome::result<void> apply_impl(cluster_t &, apply_controller_t &) const noexcept override;
    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;

    std::string folder_id;

  private:
};

} // namespace syncspirit::model::diff::modify
