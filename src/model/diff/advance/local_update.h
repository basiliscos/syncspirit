// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include "advance.h"
#include "model/folder_info.h"

namespace syncspirit::model::diff::advance {

struct SYNCSPIRIT_API local_update_t final : advance_t {
    using parent_t = advance_t;
    using parent_t::parent_t;

    local_update_t(const cluster_t &cluster, sequencer_t &sequencer, proto::FileInfo proto_file,
                   std::string_view folder_id) noexcept;

    outcome::result<void> apply_impl(cluster_t &, apply_controller_t &, void *) const noexcept override;
    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;

  private:
    model::file_info_ptr_t get_original(const model::folder_infos_map_t &fis, const model::device_t &self,
                                        const proto::FileInfo &local_file) const noexcept;
};

} // namespace syncspirit::model::diff::advance
