// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include "../cluster_diff.h"
#include "model/file_info.h"
#include "model/misc/sequencer.h"

namespace syncspirit::model::diff::modify {

struct SYNCSPIRIT_API finish_file_ack_t final : cluster_diff_t {
    finish_file_ack_t(const model::file_info_t &file, sequencer_t &sequencer) noexcept;

    outcome::result<void> apply_impl(cluster_t &) const noexcept override;
    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;

    proto::FileInfo proto_file;
    std::string folder_id;
    std::string peer_id;
    std::string file_name;
    uuid_t uuid;
};

} // namespace syncspirit::model::diff::modify
