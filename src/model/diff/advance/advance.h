// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include "../cluster_diff.h"
#include "model/file_info.h"
#include "model/misc/sequencer.h"
#include "bep.pb.h"

namespace syncspirit::model::diff::advance {

enum class advance_action_t {
    remote_copy,
    resurrect_local,
    resolve_remote_win,
    resolve_local_win,
};

struct SYNCSPIRIT_API advance_t : cluster_diff_t {
    static cluster_diff_ptr_t create(const model::file_info_t &source, sequencer_t &sequencer) noexcept;

    outcome::result<void> apply_impl(cluster_t &) const noexcept override;

    proto::FileInfo proto_file;
    std::string folder_id;
    std::string peer_id;
    bu::uuid uuid;

  protected:
    advance_t(proto::FileInfo proto_file, std::string_view folder_id, std::string_view peer_id, bu::uuid uuid) noexcept;
};

} // namespace syncspirit::model::diff::advance
