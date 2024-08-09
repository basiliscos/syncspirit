// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2023 Ivan Baidakou

#pragma once

#include "../cluster_diff.h"
#include "model/misc/sequencer.h"
#include "model/device.h"
#include "model/folder.h"
#include "model/unknown_folder.h"

namespace syncspirit::model::diff::modify {

struct SYNCSPIRIT_API share_folder_t final : cluster_diff_t {

    static outcome::result<cluster_diff_ptr_t> create(cluster_t &, sequencer_t &sequencer, const model::device_t &,
                                                      const model::folder_t &folder) noexcept;

    outcome::result<void> apply_impl(cluster_t &) const noexcept override;
    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;

    std::string peer_id;

  private:
    share_folder_t(const uuid_t &uuid, std::string_view device_id, std::string_view folder_id, std::uint64_t index_id,
                   std::int64_t max_sequence, model::unknown_folder_ptr_t uf) noexcept;
};

} // namespace syncspirit::model::diff::modify
