// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include "../cluster_diff.h"
#include "model/device.h"
#include "model/folder_info.h"
#include "model/misc/sequencer.h"
#include "proto/proto-fwd.hpp"

namespace syncspirit::model::diff::modify {

struct SYNCSPIRIT_API upsert_folder_t final : cluster_diff_t {

    static outcome::result<cluster_diff_ptr_t> create(const cluster_t &cluster, sequencer_t &sequencer, db::Folder db,
                                                      std::uint64_t index_id) noexcept;

    outcome::result<void> apply_impl(apply_controller_t &, void *) const noexcept override;
    outcome::result<void> apply_forward(apply_controller_t &, void *) const noexcept override;
    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;

    bu::uuid uuid;
    db::Folder db;

  private:
    upsert_folder_t(sequencer_t &sequencer, bu::uuid uuid, db::Folder db, model::folder_info_ptr_t folder_info,
                    const model::device_id_t &device, std::uint64_t index_id) noexcept;
};

} // namespace syncspirit::model::diff::modify
