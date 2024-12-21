// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include "../cluster_diff.h"
#include "model/file_info.h"
#include "model/device.h"
#include "model/misc/resolver.h"

namespace syncspirit::model::diff::modify {

struct SYNCSPIRIT_API finish_file_t final : cluster_diff_t {
    finish_file_t(const model::file_info_t &file) noexcept;

    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;

    std::string folder_id;
    std::string peer_id;
    std::string file_name;
    model::advance_action_t action;
};

} // namespace syncspirit::model::diff::modify
