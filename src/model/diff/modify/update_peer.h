// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2023 Ivan Baidakou

#pragma once

#include "../cluster_diff.h"
#include "structs.pb.h"

namespace syncspirit::model::diff::modify {

struct SYNCSPIRIT_API update_peer_t final : cluster_diff_t {

    template <typename T>
    update_peer_t(T &&item_, std::string_view peer_id_) noexcept : item{std::forward<T>(item_)}, peer_id{peer_id_} {}

    outcome::result<void> apply_impl(cluster_t &) const noexcept override;
    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;

    db::Device item;
    std::string peer_id;
};

} // namespace syncspirit::model::diff::modify
