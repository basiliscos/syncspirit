// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include "../cluster_diff.h"
#include "model/file_info.h"
#include "model/misc/sequencer.h"
#include "model/misc/resolver.h"
#include "bep.pb.h"

namespace syncspirit::model::diff::advance {

struct SYNCSPIRIT_API advance_t : cluster_diff_t {
    static cluster_diff_ptr_t create(advance_action_t action, const model::file_info_t &source,
                                     sequencer_t &sequencer) noexcept;

    outcome::result<void> apply_impl(cluster_t &) const noexcept override;

    proto::FileInfo proto_source;
    proto::FileInfo proto_local;
    std::string folder_id;
    std::string peer_id;
    bu::uuid uuid;
    advance_action_t action;
    bool disable_blocks_removal;

  protected:
    advance_t(std::string_view folder_id, std::string_view peer_id, advance_action_t action,
              bool disable_blocks_removal = false) noexcept;
    void initialize(const cluster_t &cluster, sequencer_t &sequencer, proto::FileInfo proto_source,
                    std::string_view local_file_name) noexcept;
};

} // namespace syncspirit::model::diff::advance
