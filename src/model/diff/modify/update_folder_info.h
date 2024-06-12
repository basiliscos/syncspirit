// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#pragma once

#include "model/device.h"
#include "model/folder.h"
#include "../cluster_diff.h"
#include "structs.pb.h"

namespace syncspirit::model::diff::modify {

struct SYNCSPIRIT_API update_folder_info_t final : cluster_diff_t {
    update_folder_info_t(db::FolderInfo db, const device_t &device, const folder_t &folder) noexcept;

    outcome::result<void> apply_impl(cluster_t &) const noexcept override;
    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;

    db::FolderInfo item;
    std::string device_id;
    std::string folder_id;
};

} // namespace syncspirit::model::diff::modify
