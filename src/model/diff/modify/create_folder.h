// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include "../cluster_diff.h"
#include "model/device.h"
#include "model/misc/sequencer.h"
#include "structs.pb.h"

namespace syncspirit::model::diff::modify {

struct SYNCSPIRIT_API create_folder_t final : cluster_diff_t {

    static outcome::result<cluster_diff_ptr_t> create(const cluster_t &cluster, sequencer_t &sequencer,
                                                      db::Folder db) noexcept;

    outcome::result<void> apply_impl(cluster_t &) const noexcept override;
    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;

  private:
    create_folder_t(sequencer_t &sequencer, db::Folder db, const model::device_t &device) noexcept;

    uuid_t uuid;
    db::Folder db;
};

} // namespace syncspirit::model::diff::modify
