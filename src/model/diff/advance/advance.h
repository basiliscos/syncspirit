// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include "../cluster_diff.h"
#include "model/file_info.h"
#include "model/misc/sequencer.h"
#include "model/misc/resolver.h"
#include "proto/proto-fwd.hpp"

namespace syncspirit::model::diff::advance {

struct SYNCSPIRIT_API advance_t : cluster_diff_t {
    static cluster_diff_ptr_t create(advance_action_t action, const model::file_info_t &source,
                                     const model::folder_info_t &source_fi, sequencer_t &sequencer) noexcept;

    outcome::result<void> apply_impl(apply_controller_t &, void *) const noexcept override;
    outcome::result<void> apply_forward(apply_controller_t &, void *) const noexcept override;

    proto::FileInfo proto_source;
    proto::FileInfo proto_local;
    std::string folder_id;
    utils::bytes_t peer_id;
    bu::uuid uuid;
    advance_action_t action;
    bool disable_blocks_removal;

  protected:
    advance_t(std::string_view folder_id, utils::bytes_view_t peer_id, advance_action_t action,
              bool disable_blocks_removal = false) noexcept;
    void initialize(const cluster_t &cluster, sequencer_t &sequencer, proto::FileInfo proto_source,
                    std::string_view local_file_name) noexcept;
};

} // namespace syncspirit::model::diff::advance
