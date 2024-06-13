// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#pragma once

#include "../cluster_diff.h"
#include "model/device.h"
#include <forward_list>

namespace syncspirit::model::diff::modify {

struct SYNCSPIRIT_API add_remote_folder_infos_t final : cluster_diff_t {
    struct item_t {
        std::string folder_id;
        std::uint64_t index_id;
        std::int64_t max_sequence;
    };
    using container_t = std::forward_list<item_t>;

    add_remote_folder_infos_t(const model::device_t &peer, container_t items) noexcept;
    outcome::result<void> apply_impl(cluster_t &) const noexcept override;
    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;

    std::string device_id;
    container_t container;
};

} // namespace syncspirit::model::diff::modify
