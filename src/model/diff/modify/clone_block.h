// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include "block_transaction.h"
#include "model/misc/file_block.h"

namespace syncspirit::model::diff::modify {

struct SYNCSPIRIT_API clone_block_t final : block_transaction_t {
    clone_block_t(const file_block_t &file_block) noexcept;

    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;

    utils::bytes_t source_device_id;
    std::string source_folder_id;
    std::string source_file_name;
    size_t source_block_index;
};

} // namespace syncspirit::model::diff::modify
