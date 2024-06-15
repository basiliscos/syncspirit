// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#pragma once

#include "model/device.h"
#include "model/file_info.h"
#include "generic_remove.h"

namespace syncspirit::model::diff::modify {

struct SYNCSPIRIT_API remove_files_t final : generic_remove_t {
    using generic_remove_t::generic_remove_t;

    remove_files_t(const device_t &device, const file_infos_map_t &files) noexcept;

    outcome::result<void> apply_impl(cluster_t &) const noexcept override;
    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;

    std::string device_id;
    keys_t folder_ids;
};

} // namespace syncspirit::model::diff::modify
