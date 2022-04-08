// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#pragma once

#include "../cluster_diff.h"

namespace syncspirit::model::diff::modify {

struct share_folder_t final : cluster_diff_t {

    share_folder_t(std::string_view peer_device_, std::string_view folder_id_) noexcept
        : peer_id{peer_device_}, folder_id{folder_id_} {}

    outcome::result<void> apply_impl(cluster_t &) const noexcept override;
    outcome::result<void> visit(cluster_visitor_t &) const noexcept override;

    std::string peer_id;
    std::string folder_id;
};

} // namespace syncspirit::model::diff::modify
