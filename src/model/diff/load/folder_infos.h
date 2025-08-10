// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include "common.h"
#include "proto/proto-fwd.hpp"

namespace syncspirit::model::diff::load {

struct SYNCSPIRIT_API folder_infos_t final : cluster_diff_t {
    struct item_t {
        utils::bytes_view_t key;
        db::FolderInfo db_fi;
    };

    using container_t = std::vector<item_t>;

    inline folder_infos_t(container_t &&container_) noexcept : container{std::move(container_)} {}

    outcome::result<void> apply_impl(cluster_t &, apply_controller_t &, void *) const noexcept override;

    container_t container;
};

} // namespace syncspirit::model::diff::load
