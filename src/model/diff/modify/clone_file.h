// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include "../cluster_diff.h"
#include "model/file_info.h"
#include "model/misc/sequencer.h"
#include "bep.pb.h"

namespace syncspirit::model::diff::modify {

struct SYNCSPIRIT_API clone_file_t final : cluster_diff_t {
    static cluster_diff_ptr_t create(const model::file_info_t &source, sequencer_t &sequencer) noexcept;

    clone_file_t(proto::FileInfo proto_file, std::string_view folder_id, std::string_view peer_id,
                 bu::uuid uuid) noexcept;

    outcome::result<void> apply_impl(cluster_t &) const noexcept override;
    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;

    proto::FileInfo proto_file;
    std::string folder_id;
    std::string peer_id;
    bu::uuid uuid;
};

} // namespace syncspirit::model::diff::modify
