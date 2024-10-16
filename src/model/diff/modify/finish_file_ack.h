// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include "../cluster_diff.h"
#include "model/file_info.h"

namespace syncspirit::model::diff::modify {

struct SYNCSPIRIT_API finish_file_ack_t final : cluster_diff_t {
    finish_file_ack_t(const model::file_info_t &file, std::string_view peer_id) noexcept;

    outcome::result<void> apply_impl(cluster_t &) const noexcept override;
    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;

    std::string folder_id;
    std::string peer_id;
    std::string file_name;
};

} // namespace syncspirit::model::diff::modify
