// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2023 Ivan Baidakou

#pragma once

#include "../cluster_diff.h"
#include "model/file_info.h"

namespace syncspirit::model::diff::modify {

struct SYNCSPIRIT_API lock_file_t final : cluster_diff_t {

    lock_file_t(const model::file_info_t &file, bool locked) noexcept;

    outcome::result<void> apply_impl(cluster_t &) const noexcept override;
    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;

    std::string folder_id;
    std::string device_id;
    std::string file_name;
    bool locked;
};

} // namespace syncspirit::model::diff::modify
