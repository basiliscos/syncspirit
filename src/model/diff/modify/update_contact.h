// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2023 Ivan Baidakou

#pragma once

#include "../contact_diff.h"
#include "model/cluster.h"

namespace syncspirit::model::diff::modify {

struct SYNCSPIRIT_API update_contact_t final : contact_diff_t {
    using ip_addresses_t = std::vector<std::string_view>;

    update_contact_t(const model::cluster_t &cluster, const model::device_id_t &device,
                     const utils::uri_container_t &uris) noexcept;
    update_contact_t(const model::cluster_t &cluster, const ip_addresses_t &addresses) noexcept;

    outcome::result<void> apply_impl(cluster_t &) const noexcept override;
    outcome::result<void> visit(contact_visitor_t &, void *) const noexcept override;

    model::device_id_t device;
    utils::uri_container_t uris;
    bool known;
    bool self;
};

} // namespace syncspirit::model::diff::modify
