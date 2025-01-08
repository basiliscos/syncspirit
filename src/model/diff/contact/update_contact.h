// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include "../cluster_diff.h"
#include "model/cluster.h"

namespace syncspirit::model::diff::contact {

struct SYNCSPIRIT_API update_contact_t final : cluster_diff_t {
    using ip_addresses_t = std::vector<std::string_view>;

    update_contact_t(const model::cluster_t &cluster, const model::device_id_t &device,
                     const utils::uri_container_t &uris) noexcept;

    outcome::result<void> apply_impl(cluster_t &, apply_controller_t &) const noexcept override;
    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;

    model::device_id_t device;
    utils::uri_container_t uris;
    bool known;
    bool self;
};

} // namespace syncspirit::model::diff::contact
