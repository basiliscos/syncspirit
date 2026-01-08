// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include "../cluster_diff.h"
#include "model/folder.h"
#include <boost/system.hpp>

namespace syncspirit::model::diff::modify {

namespace sys = boost::system;

struct SYNCSPIRIT_API suspend_folder_t final : cluster_diff_t {

    suspend_folder_t(const model::folder_t &folder, bool value, const sys::error_code &ec = {}) noexcept;

    outcome::result<void> apply_impl(apply_controller_t &, void *) const noexcept override;
    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;

    std::string folder_id;
    bool value;
    sys::error_code ec;
};

} // namespace syncspirit::model::diff::modify
